// Optional integration check: worker-smoke MODEL RAW_PCM16_MONO_16000
// RAW must contain Russian speech; it is never downloaded by this executable.
#include "transcription-worker.hpp"
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char **argv){
 if(argc!=3){std::cerr<<"Usage: worker-smoke model.bin russian-pcm16-mono-16000.raw\n";return 2;}
 std::ifstream raw(std::filesystem::u8path(argv[2]),std::ios::binary);
 std::vector<char> bytes((std::istreambuf_iterator<char>(raw)),{});
 if(bytes.size()<32000 || bytes.size()%2)return 2;
 std::vector<float> pcm;pcm.reserve(bytes.size()/2+16000);
 for(size_t i=0;i<bytes.size();i+=2){uint16_t u=uint8_t(bytes[i])|(uint16_t(uint8_t(bytes[i+1]))<<8);pcm.push_back(float(int16_t(u))/32768);}
 pcm.resize(pcm.size()+16000,0);
 std::mutex mutex;std::condition_variable cv;
 bool ready=false,recognizing=false,error=false;std::atomic<bool> overflow{false};std::string text;
 TranscriptionWorker worker;
 worker.start(std::filesystem::u8path(argv[1]),2,[&](WorkerEvent event){
  std::cerr<<"EVENT "<<int(event.kind)<<": "<<event.text<<'\n';
  std::lock_guard<std::mutex> lock(mutex);
  if(event.kind==WorkerEvent::Error){error=true;std::cerr<<event.text<<'\n';}
  if(event.text=="Overflow")overflow=true;
  if(event.kind==WorkerEvent::Text)text=event.text;
  if(event.text=="Listening")ready=true;
  if(event.text=="Recognizing")recognizing=true;
  cv.notify_all();
 });
 {std::unique_lock<std::mutex> lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(30),[&]{return ready||error;})||error)return 1;}
 std::cerr<<"Model ready; submitting fixture\n";
 auto submit=[&]{for(size_t i=0;i<pcm.size();i+=160){const float *p[]={pcm.data()+i};worker.submit(p,std::min(size_t(160),pcm.size()-i),1,16000);std::this_thread::sleep_for(std::chrono::milliseconds(10));}};
 submit();
 std::cerr<<"Fixture submitted; waiting for transcript\n";
 {std::unique_lock<std::mutex> lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(90),[&]{return !text.empty()||error;})||error)return 1;
  std::cout<<"TRANSCRIPT="<<text<<'\n';
  // Match UTF-8 Russian key words from the local synthetic fixture.
  if(text.find("\xD1\x80\xD1\x83\xD1\x81\xD1\x81")==std::string::npos)return 1;
  recognizing=false;
 }
 submit();
 {std::unique_lock<std::mutex> lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(30),[&]{return recognizing||error;})||error)return 1;}
 auto before=std::chrono::steady_clock::now();worker.stop();
 auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();
 std::cout<<"stop during inference: "<<ms<<" ms\n";
 std::cout<<"audio overflow: "<<(overflow?"FAIL":"NONE")<<'\n';
 return ms<10000 && !overflow?0:1;
}
