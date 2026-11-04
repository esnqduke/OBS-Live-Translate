// Real-time 48 kHz stereo input while Whisper runs; requires the local model.
#include "transcription-worker.hpp"
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <whisper.h>
namespace {
std::atomic<bool> gpu_backend_seen{false},gpu_backend_failed{false};
void backend_log(ggml_log_level,const char *text,void *) {
 const std::string line(text);
 if(line.find("whisper_backend_init_gpu: using Vulkan")!=std::string::npos)gpu_backend_seen=true;
 if(line.find("whisper_backend_init_gpu: failed")!=std::string::npos)gpu_backend_failed=true;
 std::cerr<<text;
}
}
int main(int argc,char **argv){
 if(argc<3 || argc>7)return 2;
 bool english=false,gpu=false,quality=false,cancel_partial=false;
 for(int i=3;i<argc;i++){
  if(std::string(argv[i])=="--translate")english=true;
  else if(std::string(argv[i])=="--gpu")gpu=true;
  else if(std::string(argv[i])=="--quality")quality=true;
  else if(std::string(argv[i])=="--cancel-partial")cancel_partial=true;
  else return 2;
 }
 if(quality && !english)return 2;
 if(cancel_partial && !english)return 2;
 const int repeats=quality || cancel_partial?1:(english?2:4);
 whisper_log_set(backend_log,nullptr);
 std::ifstream file(std::filesystem::u8path(argv[2]),std::ios::binary);
 std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
 if(bytes.size()<32000 || bytes.size()%2)return 2;
 std::vector<float> fixture;
 for(size_t i=0;i<bytes.size();i+=2){uint16_t u=uint8_t(bytes[i])|(uint16_t(uint8_t(bytes[i+1]))<<8);fixture.push_back(float(int16_t(u))/32768);}
 fixture.resize(fixture.size()+16000,0);
 std::vector<float> input;
 for(int repeat=0;repeat<repeats;repeat++)input.insert(input.end(),fixture.begin(),fixture.end());
 SpeechSegmenter reference;size_t expected=0,first_completion=0;
 for(size_t i=0;i<input.size();i+=160){
  auto end=std::min(i+160,input.size());
  if(!reference.feed(std::vector<float>(input.begin()+i,input.begin()+end)).empty()){++expected;if(!first_completion)first_completion=end;}
 }
 std::mutex mutex;std::condition_variable cv;bool ready=false,error=false;
 size_t texts=0,transcribed=0,completed=0;uint64_t dropped=0,captured=0,converted=0;
 size_t english_speech=0,english_internet=0;bool cyrillic=false,gpu_seen=false,warmup_seen=false;
 size_t partials=0;long long first_partial_ms=-1;bool partial_cyrillic=false;
 uint64_t last_final_id=0;bool phrase_ids_valid=true;
 uint64_t cancelled_id=0;bool cancellation_seen=false;std::atomic<bool> cancellation_requested{false};
 std::chrono::steady_clock::time_point input_started;
 TranscriptionWorker worker;
 worker.start(std::filesystem::u8path(argv[1]),2,[&](WorkerEvent event){
  std::lock_guard<std::mutex> lock(mutex);
  if(event.kind==WorkerEvent::Error || event.text=="Overflow"){error=true;std::cerr<<event.text<<'\n';}
  if(event.text=="Listening" || event.text=="ListeningEnglish")ready=true;
  if(event.kind==WorkerEvent::PartialText){
   if(cancel_partial && !cancelled_id){cancelled_id=event.utterance;cancellation_requested=true;}
   if(!event.utterance || event.utterance<=last_final_id)phrase_ids_valid=false;
   if(!ready)error=true;
   if(first_partial_ms<0)first_partial_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-input_started).count();
   ++partials;std::cout<<"PARTIAL "<<partials<<": "<<event.text<<std::endl;
   for(unsigned char c:event.text)if(c==0xD0 || c==0xD1)partial_cyrillic=true;
  }
  if(event.kind==WorkerEvent::Text){
   if(cancel_partial && event.utterance==cancelled_id && event.text.empty())cancellation_seen=true;
   if(!event.utterance || event.utterance<=last_final_id)phrase_ids_valid=false;
   last_final_id=event.utterance;
   if(!ready)error=true;
   ++texts;std::cout<<"TEXT "<<texts<<": "<<event.text<<std::endl;
   std::string lower=event.text;
   std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
   if(lower.find("russian")!=std::string::npos)++english_speech;
   if(lower.find("internet")!=std::string::npos)++english_internet;
   for(unsigned char c:event.text)if(c==0xD0 || c==0xD1)cyrillic=true;
  }
  if(event.kind==WorkerEvent::Diagnostic){
   if(event.text.find("GpuWarmup ms=")==0)warmup_seen=true;
   if(event.text.find("RequestedGpu ")==0 && event.text.find("NVIDIA")!=std::string::npos)gpu_seen=true;
   std::cout<<event.text<<std::endl;
   std::istringstream fields(event.text);std::string field;
   while(fields>>field){auto split=field.find('=');if(split==std::string::npos)continue;
    auto key=field.substr(0,split);auto value=std::stoull(field.substr(split+1));
    if(key=="transcribed")transcribed=size_t(value);if(key=="completed")completed=size_t(value);
    if(key=="dropped_blocks")dropped=value;if(key=="captured_frames")captured=value;if(key=="converted_frames")converted=value;
   }
  }
  cv.notify_all();
 },english,gpu);
 {std::unique_lock<std::mutex> lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(120),[&]{return ready||error;})||error)return 1;}
 std::cout<<"expected_segments="<<expected<<" input_seconds="<<double(input.size())/16000<<std::endl;
 auto start=std::chrono::steady_clock::now();
 {std::lock_guard<std::mutex> lock(mutex);input_started=start;}
 std::array<float,480> left{},right{};
 bool rejected=false,modified=false;
 for(size_t i=0;i<input.size();i+=160){
  size_t count=std::min(size_t(160),input.size()-i);
  for(size_t j=0;j<count;j++)for(size_t k=0;k<3;k++)left[j*3+k]=right[j*3+k]=input[i+j];
  bool cancel_now=cancellation_requested.load();
  if(cancel_now){left.fill(0);right.fill(0);}
  const float *planes[]={left.data(),right.data()};
  auto original_left=left,original_right=right;
  if(!worker.submit(planes,count*3,cancel_now?1:2,48000))rejected=true;
  if(left!=original_left || right!=original_right)modified=true;
  std::this_thread::sleep_until(start+std::chrono::microseconds((i+count)*1000000/16000));
 }
 bool drained;
 {std::unique_lock<std::mutex> lock(mutex);drained=cv.wait_for(lock,std::chrono::seconds(90),[&]{return error || (cancel_partial?cancellation_seen:completed==expected && transcribed==expected);});}
 // stop() intentionally cancels capture immediately. Allow the independent
 // 2-ms collector to consume the last submitted silence block first. The
 // exact captured/converted equality below remains mandatory after stopping.
 std::this_thread::sleep_for(std::chrono::milliseconds(100));
 worker.stop();
 if(cancel_partial){
  bool pass=drained && cancellation_seen && phrase_ids_valid && !error && !rejected && !modified && dropped==0 && captured==input.size()*3 && converted==captured;
  if(gpu)pass=pass && gpu_seen && warmup_seen && gpu_backend_seen && !gpu_backend_failed;
  std::cout<<"CANCELLED_PARTIAL="<<(pass?"PASS":"FAIL")<<" utterance="<<cancelled_id<<" ordered_empty_final="<<cancellation_seen<<" PCM="<<(modified?"MODIFIED":"UNCHANGED")<<std::endl;
  return pass?0:1;
 }
 bool pass=phrase_ids_valid && drained && !error && !rejected && !modified && dropped==0 && completed==expected && transcribed==expected && texts==expected && captured==input.size()*3 && converted==captured;
 std::cout<<"PHRASE_IDS="<<(phrase_ids_valid?"PASS":"FAIL")<<std::endl;
 if(english)pass=pass && !cyrillic && (quality || (english_speech==size_t(repeats) && english_internet==size_t(repeats)));
 else pass=pass && cyrillic && english_speech==0 && english_internet==0;
 if(gpu)pass=pass && gpu_seen && warmup_seen && gpu_backend_seen && !gpu_backend_failed;
 const auto first_speech_end_ms=static_cast<long long>(first_completion*1000/16000)-700;
 if(gpu && english)pass=pass && partials>0 && !partial_cyrillic && first_partial_ms>=0 && first_partial_ms<first_speech_end_ms;
 std::cout<<"PARTIALS="<<partials<<" FIRST_PARTIAL_MS="<<first_partial_ms<<" FIRST_SPEECH_END_MS="<<first_speech_end_ms<<std::endl;
 std::cout<<"GPU_REQUESTED="<<gpu<<" NVIDIA_SELECTED="<<gpu_seen
          <<" VULKAN_BACKEND="<<gpu_backend_seen<<" GPU_INIT_FAILED="<<gpu_backend_failed<<std::endl;
 std::cout<<"MODE="<<(english?"EN":"RU")<<" INPUT_AUDIO="<<(modified?"MODIFIED":"UNCHANGED")<<" English_speech="<<english_speech<<" English_internet="<<english_internet<<std::endl;
 std::cout<<"CONTINUITY="<<(pass?"PASS":"FAIL")<<" expected="<<expected<<" completed="<<completed<<" transcribed="<<transcribed<<" texts="<<texts<<" captured="<<captured<<" converted="<<converted<<" dropped="<<dropped<<std::endl;
 return pass?0:1;
}
