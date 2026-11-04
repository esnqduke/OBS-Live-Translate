#include "audio-resampler.hpp"
#include "transcription-worker.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <condition_variable>

void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main()
{
 try {
  AudioBlockQueue q;
  float a[480] = {}, b[480] = {};
  const float *planes[] = {a,b};
  check(!q.try_push(planes, 480, 0, 48000), "reject zero channels");
  check(!q.try_push(planes, 480, 2, 0), "reject zero rate");
  check(!q.try_push(planes, 99999, 2, 48000), "reject oversized block");
  const float *bad[] = {nullptr};
  check(!q.try_push(bad, 480, 1, 48000), "reject null plane");
  for(size_t i=0;i<AudioBlockQueue::capacity;i++) {
   a[0]=float(i); check(q.try_push(planes,480,2,48000), "accept block");
  }
  a[0]=99; check(q.try_push(planes,480,2,48000), "replace oldest on overflow");
  check(q.dropped()>0, "count overflow");
  AudioBlock block;
  check(q.pop(block) && block.samples[0]==1, "FIFO after eviction and owned copy");
  check(block.generation==0,"retain pre-gap generation in queued block");
  check(block.discontinuity,"eviction marks first remaining block as discontinuous");
  for(size_t i=2;i<AudioBlockQueue::capacity;i++)check(q.pop(block) && block.generation==0,"old backlog retains generation");
  check(q.pop(block) && block.generation==1,"post-gap audio has new generation");
  q.clear(); check(!q.pop(block), "clear queue");
  {
   AudioBlockQueue concurrent;std::atomic<bool> done{false},bad{false};std::atomic<size_t> consumed{0};
   std::thread consumer([&]{AudioBlock b;float previous=-1;
    for(;;){if(concurrent.pop(b)){
     if(b.samples[0]<=previous || b.samples[0]!=b.samples[479])bad=true;
     previous=b.samples[0];++consumed;
    }else if(done)break;else std::this_thread::yield();}
   });
   for(int i=0;i<10000;i++){std::fill_n(a,480,float(i));const float *p[]={a};concurrent.try_push(p,480,1,48000);std::this_thread::yield();}
   done=true;consumer.join();check(consumed>0 && !bad,"concurrent FIFO without torn blocks during saturation");
  }
  for(uint32_t rate : {16000u,44100u,48000u}) {
   AudioResampler r;
   size_t count=0;
   for(int n=0;n<100;n++) {
    AudioBlock x; x.frames=rate/100; x.channels=2; x.rate=rate;
    for(size_t i=0;i<x.frames;i++){x.samples[i]=0.25f;x.samples[AudioBlock::max_frames+i]=0.75f;}
    auto out=r.convert(x); count+=out.size();
    for(auto v:out) check(std::abs(v-0.5f)<0.0001f,"stereo mix constant");
   }
   check(count>=15999 && count<=16000,"one second resampling across blocks");
  }
  AudioResampler r; AudioBlock mono; mono.frames=480; mono.channels=1; mono.rate=16000;
  for(size_t i=0;i<mono.frames;i++) mono.samples[i]=float(i)/480;
  auto out=r.convert(mono); check(out.size()==480,"mono count");
  check(out[100]==mono.samples[100],"mono unchanged");
  SpeechSegmenter seg; std::vector<float> phrase;
  for(int i=0;i<40;i++) check(seg.feed(std::vector<float>(160,0.1f)).empty(),"speech spans blocks");
  for(int i=0;i<80;i++){auto s=seg.feed(std::vector<float>(160,0));if(!s.empty())phrase=std::move(s);}
  check(!phrase.empty(),"silence flush");
  seg.reset();
  for(int i=0;i<200;i++)check(seg.feed(std::vector<float>(160,0.001f)).empty(),"ignore noise");
  bool forced=false;for(int i=0;i<1600;i++)if(!seg.feed(std::vector<float>(160,0.2f)).empty())forced=true;
  check(forced,"maximum duration flush");
  seg.reset();
  for(int i=0;i<99;i++){seg.feed(std::vector<float>(160,0.1f));check(seg.take_partial().empty(),"no premature short partial");}
  seg.feed(std::vector<float>(160,0.1f));
  check(seg.take_partial().size()==16000,"partial delivered during speech before pause");
  check(seg.take_partial().empty(),"partial cadence bounded");
  for(int i=0;i<100;i++)seg.feed(std::vector<float>(160,0.1f));
  check(seg.take_partial().size()==32000,"partial accumulates translation context");
  phrase.clear();for(int i=0;i<70;i++){auto s=seg.feed(std::vector<float>(160,0));if(!s.empty())phrase=std::move(s);}
  check(phrase.size()==43200 && seg.take_partial().empty(),"pause final retains all PCM, resets partial");
  seg.feed(std::vector<float>(160,0.1f));seg.reset();check(seg.take_partial().empty(),"discontinuity cancels snapshot");
  for(bool corrupt : {false,true}) {
   auto file=std::filesystem::temp_directory_path()/"obs-stt-test-corrupt.bin";
   if(corrupt){std::ofstream f(file);f<<"not a model";}
   else std::filesystem::remove(file);
   std::mutex m; std::condition_variable cv; bool failed=false;TranscriptionWorker w;
   w.start(file,2,[&](WorkerEvent e){if(e.kind==WorkerEvent::Error){std::lock_guard<std::mutex> lock(m);failed=true;cv.notify_one();}});
   {std::unique_lock<std::mutex> lock(m);check(cv.wait_for(lock,std::chrono::seconds(10),[&]{return failed;}),"invalid model error");}
   w.stop(); w.stop();std::filesystem::remove(file);
  }
  std::cout<<"queue, conversion, segmentation, missing/corrupt model: PASS\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
