// Optional real-Whisper check: worker-mute MODEL PCM16_MONO_16000.raw [--gpu]
#include "transcription-worker.hpp"
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
void check(bool v,const char *s){if(!v)throw std::runtime_error(s);}
int main(int argc,char **argv){try{
 if(argc!=3 && argc!=4)return 2;const bool gpu=argc==4 && std::string(argv[3])=="--gpu";
 std::ifstream input(std::filesystem::u8path(argv[2]),std::ios::binary);
 std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});check(bytes.size()>32000 && bytes.size()%2==0,"PCM fixture missing");
 std::vector<float> pcm;for(size_t i=0;i<bytes.size();i+=2){uint16_t v=uint8_t(bytes[i])|(uint16_t(uint8_t(bytes[i+1]))<<8);pcm.push_back(float(int16_t(v))/32768);}
 pcm.resize(pcm.size()+16000,0);
 TranscriptionWorker worker;std::mutex mutex;std::condition_variable cv;
 bool ready=false,processing=false,failed=false;int model_loads=0;std::vector<WorkerEvent> texts;
 worker.set_paused(true);
 worker.start(std::filesystem::u8path(argv[1]),2,[&](WorkerEvent e){
  std::lock_guard<std::mutex> lock(mutex);
  if(e.kind==WorkerEvent::Error){failed=true;std::cerr<<e.text<<'\n';}
  if(e.text=="Listening")ready=true;if(e.text=="Recognizing")processing=true;
  if(e.text.find("VerifiedModel ")==0)++model_loads;
  if(e.kind==WorkerEvent::Text || e.kind==WorkerEvent::PartialText)texts.push_back(e);
  cv.notify_all();
 },false,gpu);
 {std::unique_lock<std::mutex> lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(40),[&]{return ready||failed;}) && !failed,"model failed to load");}
 const float *plane[]={pcm.data()};check(!worker.submit(plane,160,1,16000),"muted worker must reject audio");
 worker.set_paused(false);
 auto feed=[&]{for(size_t i=0;i<pcm.size();i+=160){const float *p[]={pcm.data()+i};check(worker.submit(p,std::min<size_t>(160,pcm.size()-i),1,16000),"fresh audio must be accepted");std::this_thread::sleep_for(std::chrono::milliseconds(10));}};
 feed();
 {std::unique_lock<std::mutex> lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(20),[&]{return processing||failed;}) && !failed,"recognition did not start");}
 std::this_thread::sleep_for(std::chrono::milliseconds(20));worker.set_paused(true);
 {std::lock_guard<std::mutex> lock(mutex);texts.clear();}
 std::this_thread::sleep_for(std::chrono::milliseconds(400));
 check(!worker.submit(plane,160,1,16000),"mute during inference rejects audio");
 // Rapid pair must still cancel the old generation before the capture thread sees it.
 worker.set_paused(false);worker.set_paused(true);worker.set_paused(false);
 const auto resumed=worker.generation();std::this_thread::sleep_for(std::chrono::milliseconds(400));
 {std::lock_guard<std::mutex> lock(mutex);check(texts.empty(),"cancelled audio must not reappear after unmute");}
 feed();
 {std::unique_lock<std::mutex> lock(mutex);check(cv.wait_for(lock,std::chrono::seconds(60),[&]{return !texts.empty()||failed;}) && !failed,"fresh speech did not produce text");
  check(texts.front().generation==resumed && !texts.front().text.empty(),"only fresh generation may publish");check(model_loads==1,"mute must keep Whisper model loaded");}
 worker.stop();std::cout<<"real Whisper: startup mute, in-flight cancellation, rapid toggles, fresh speech, single model load: PASS\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
