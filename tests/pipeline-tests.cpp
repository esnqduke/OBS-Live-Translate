#include "transcription-worker.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(){try{
 CompletedSegmentQueue q;std::vector<float> out;
 check(!q.pop(out),"empty phrase queue");
 // Inference is deliberately absent while capture deposits 120 seconds.
 for(size_t i=0;i<120;i++){
  std::vector<float> phrase(16000,float(i));
  check(q.push(std::move(phrase)),"capture retains complete phrase while inference is busy");
 }
 check(q.pending_samples()==120*16000,"bounded backlog measured in 16 kHz samples");
 std::vector<float> extra(16000,999);
 check(!q.push(std::move(extra)) && extra.size()==16000,"overload rejects new phrase without evicting existing speech");
 for(size_t i=0;i<120;i++){
  check(q.pop(out) && out.size()==16000 && out.front()==float(i) && out.back()==float(i),"all buffered phrases preserved in FIFO order");
 }
 check(q.pending_samples()==0 && !q.pop(out),"drained backlog");
 {
  CompletedSegmentQueue stream;RecognitionInput job;
  stream.push_partial(std::vector<float>(16000,1),1);
  stream.push_partial(std::vector<float>(32000,2),1);
  check(stream.pop(job) && !job.final && job.pcm.size()==32000,"one replaceable partial, latest snapshot");
  check(!stream.pop(out),"partial never accumulates duplicate work");
  check(stream.push(std::vector<float>(48000,3),1),"final retained");
  check(!stream.current_partial(1),"in-flight partial obsolete as soon as final captured");
  stream.push_partial(std::vector<float>(16000,4),2);
  check(stream.pop(job) && job.final && job.utterance==1,"complete utterances take priority");
  check(stream.pop(job) && !job.final && job.utterance==2,"new utterance partial follows final");
  stream.cancel_partial(2);check(!stream.current_partial(2),"gap invalidates in-flight snapshot");
  stream.push_partial(std::vector<float>(16000,5),2);
  check(!stream.pop(job),"late snapshot cannot revive finalized/cancelled utterance");
 }
 {
  CompletedSegmentQueue reset;RecognitionInput job;
  reset.push(std::vector<float>(16000,1),10,4);reset.push_partial(std::vector<float>(16000,2),11,4);
  reset.clear(11);check(!reset.pop(job) && reset.pending_samples()==0 && !reset.current_partial(11),"mute discards finals and drafts and invalidates active partial");
  reset.push(std::vector<float>(16000,3),12,8);
  check(reset.pop(job) && job.generation==8 && job.pcm.front()==3,"new capture generation survives reset");
 }
 std::atomic<bool> done{false},bad{false};std::atomic<size_t> count{0};
 {
  CompletedSegmentQueue cancelled;RecognitionInput job;
  check(cancelled.push(std::vector<float>(16000,1),1),"older final queued");
  cancelled.push_partial(std::vector<float>(16000,2),2);
  check(cancelled.finish_partial(2),"cancelled partial receives ordered final marker");
  check(cancelled.pop(job) && job.utterance==1 && !job.pcm.empty(),"cancellation cannot overtake older final");
  check(cancelled.pop(job) && job.utterance==2 && job.final && job.pcm.empty(),"empty final marker seals abandoned draft");
  check(!cancelled.current_partial(2) && cancelled.pending_samples()==0,"cancel marker invalidates snapshot without audio sample accounting");
 }
 std::thread consumer([&]{std::vector<float> phrase;size_t expected=0;
  while(!done || q.pending_samples()){
   if(q.pop(phrase)){if(phrase.front()!=float(expected++) || phrase.back()!=phrase.front())bad=true;++count;}
   else std::this_thread::yield();
  }
 });
 for(int i=0;i<1000;i++){std::vector<float> phrase(160,float(i));check(q.push(std::move(phrase)),"concurrent enqueue");}
 done=true;consumer.join();check(!bad && count==1000,"concurrent phrase delivery loses none");
 std::cout<<"phrase retention during blocked inference, bounds and concurrent FIFO: PASS\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
