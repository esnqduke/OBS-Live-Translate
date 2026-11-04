#include "transcription-worker.hpp"
#include <whisper.h>
#include "supported-models.hpp"
#include <ggml-backend.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
const SupportedModel *supported_model(FILE *file,const std::atomic<bool> &stopping)
{
 // Upstream's parser assumes internally consistent data and can assert on
 // corrupt headers. Only the exact documented model may reach that parser.
 uint64_t bytes=0;
 if(!model_file_size(file,bytes))return nullptr;
 bool known=false;for(const auto &model:supported_models)if(bytes==model.bytes)known=true;
 if(!known)return nullptr;
 if(!rewind_model_file(file))return nullptr;
#ifdef _WIN32
 struct Hash {
  BCRYPT_ALG_HANDLE algorithm=nullptr;
  BCRYPT_HASH_HANDLE hash=nullptr;
  ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
 } state;
 if(BCryptOpenAlgorithmProvider(&state.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
    BCryptCreateHash(state.algorithm,&state.hash,nullptr,0,nullptr,0,0)<0)return nullptr;
 std::array<unsigned char,65536> buffer{};
 size_t count;
 while((count=std::fread(buffer.data(),1,buffer.size(),file))!=0){
  if(stopping || BCryptHashData(state.hash,buffer.data(),static_cast<ULONG>(count),0)<0)return nullptr;
 }
 std::array<unsigned char,32> digest{};
 if(std::ferror(file) || BCryptFinishHash(state.hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)return nullptr;
 const char *hex="0123456789abcdef";
 std::string actual;for(auto v:digest){actual+=hex[v>>4];actual+=hex[v&15];}
 if(!rewind_model_file(file))return nullptr;
 return verified_model(bytes,actual);
#else
 (void)stopping;
 // Windows is the supported runtime for this Text (GDI+) filter.
 return nullptr;
#endif
}
}

bool CompletedSegmentQueue::push(std::vector<float> &&phrase,uint64_t utterance)
{
 std::lock_guard<std::mutex> lock(mutex);
 if(phrase.empty() || phrase.size()>max_samples-samples)return false;
 auto count=phrase.size();phrases.push_back({std::move(phrase),utterance,true});samples+=count;
 finalized=std::max(finalized,utterance);
 if(partial.utterance<=finalized)partial.pcm.clear();
 return true;
}
void CompletedSegmentQueue::push_partial(std::vector<float> &&phrase,uint64_t utterance)
{
 std::lock_guard<std::mutex> lock(mutex);
 if(utterance<=finalized || phrase.empty() || phrase.size()>240000)return;
 partial={std::move(phrase),utterance,false};
}
void CompletedSegmentQueue::cancel_partial(uint64_t utterance)
{
 std::lock_guard<std::mutex> lock(mutex);
 finalized=std::max(finalized,utterance);
 if(partial.utterance<=finalized)partial.pcm.clear();
}
bool CompletedSegmentQueue::current_partial(uint64_t utterance) const
{
 std::lock_guard<std::mutex> lock(mutex);return utterance>finalized;
}
bool CompletedSegmentQueue::finish_partial(uint64_t utterance)
{
 std::lock_guard<std::mutex> lock(mutex);
 if(utterance<=finalized)return true;
 if(phrases.size()>=512)return false;
 finalized=utterance;if(partial.utterance<=finalized)partial.pcm.clear();
 phrases.push_back({{},utterance,true});return true;
}
bool CompletedSegmentQueue::pop(RecognitionInput &input)
{
 std::lock_guard<std::mutex> lock(mutex);
 if(!phrases.empty()){
  input=std::move(phrases.front());phrases.pop_front();samples-=input.pcm.size();return true;
 }
 if(partial.pcm.empty())return false;
 input=std::move(partial);partial=RecognitionInput{};return true;
}
bool CompletedSegmentQueue::pop(std::vector<float> &phrase)
{
 RecognitionInput input;if(!pop(input))return false;
 phrase=std::move(input.pcm);return true;
}
size_t CompletedSegmentQueue::pending_samples() const
{
 std::lock_guard<std::mutex> lock(mutex);return samples;
}

void TranscriptionWorker::start(const std::filesystem::path &model,unsigned threads,ResultCallback callback,bool translate_to_english,bool use_gpu)
{
 stop();captured_frames=0;stopping=false;
 thread=std::thread([this,model,threads,translate_to_english,use_gpu,callback=std::move(callback)]{
  try{run(model,threads,callback,translate_to_english,use_gpu);}
  catch(const std::exception &e){callback({WorkerEvent::Error,e.what()});}
  catch(...){callback({WorkerEvent::Error,"Unexpected worker failure"});}
  accepting=false;
 });
}
void TranscriptionWorker::stop()
{
 accepting=false;stopping=true;
 if(thread.joinable())thread.join();
 queue.clear();
}
void TranscriptionWorker::run(const std::filesystem::path &path,unsigned threads,const ResultCallback &callback,bool translate_to_english,bool use_gpu)
{
 callback({WorkerEvent::Status,"Loading"});
 // Custom loader supports Unicode Windows paths without allocating the whole model.
#ifdef _WIN32
 FILE *raw=_wfopen(path.c_str(),L"rb");
#else
 FILE *raw=std::fopen(path.c_str(),"rb");
#endif
 if(!raw){callback({WorkerEvent::Error,"ModelMissing"});return;}
 std::unique_ptr<FILE,decltype(&std::fclose)> file(raw,&std::fclose);
 auto *model=supported_model(raw,stopping);
 if(!model){
  if(!stopping)callback({WorkerEvent::Error,"ModelInvalid"});
  return;
 }
 callback({WorkerEvent::Diagnostic,std::string("VerifiedModel ")+model->name});
 whisper_model_loader loader{};
 loader.context=raw;
 loader.read=[](void *p,void *out,size_t n){return std::fread(out,1,n,static_cast<FILE *>(p));};
 loader.eof=[](void *p){return std::feof(static_cast<FILE *>(p))!=0;};
 loader.close=[](void *){};
 auto cp=whisper_context_default_params();cp.use_gpu=use_gpu;
 std::string selected_device="CPU";
 if(use_gpu){
  // Static registry only: never scan OBS/CWD for third-party backend DLLs.
  int index=0,selected=-1;
  for(size_t i=0;i<ggml_backend_dev_count();i++){
   auto device=ggml_backend_dev_get(i);
   if(ggml_backend_dev_type(device)!=GGML_BACKEND_DEVICE_TYPE_GPU)continue;
   const std::string description=ggml_backend_dev_description(device);
   if(selected<0 || description.find("NVIDIA")!=std::string::npos){
    selected=index;selected_device=std::string(ggml_backend_dev_name(device))+" "+description;
    if(description.find("NVIDIA")!=std::string::npos)break;
   }
   ++index;
  }
  // Whisper 1.7.6 can put weights on GPU0 when a later GPU is selected.
  // Reject that configuration rather than mixing devices silently.
  if(selected!=0){callback({WorkerEvent::Error,"GpuUnavailable"});return;}
  cp.gpu_device=selected;
  callback({WorkerEvent::Diagnostic,"RequestedGpu "+selected_device});
 }
 std::unique_ptr<whisper_context,decltype(&whisper_free)> ctx(whisper_init_with_params(&loader,cp),&whisper_free);
 if(!ctx){callback({WorkerEvent::Error,use_gpu?"GpuInitFailed":"ModelInvalid"});return;}
 if(!whisper_is_multilingual(ctx.get())){callback({WorkerEvent::Error,"ModelInvalid"});return;}
 callback({WorkerEvent::Diagnostic,"ComputeRequest "+selected_device});
 if(stopping)return;
 auto inference_params=[&]{
  auto params=whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.language="ru";params.translate=translate_to_english;params.no_context=true;params.no_timestamps=true;
  params.print_progress=params.print_realtime=params.print_timestamps=params.print_special=false;
  params.n_threads=int(std::clamp(threads,1u,32u));
  params.abort_callback=[](void *p){return static_cast<TranscriptionWorker *>(p)->stopping.load();};
  params.abort_callback_user_data=this;
  return params;
 };
 if(use_gpu){
  // Driver pipelines are lazy-created. Warm encoder/decoder before accepting
  // microphone audio, and never publish the synthetic silence as subtitles.
  callback({WorkerEvent::Status,"WarmingGpu"});
  std::vector<float> silence(16000,0.0f);
  auto params=inference_params();params.max_tokens=1;
  auto before=std::chrono::steady_clock::now();
  int result=whisper_full(ctx.get(),params,silence.data(),int(silence.size()));
  if(stopping)return;
  if(result!=0){callback({WorkerEvent::Error,"GpuInitFailed"});return;}
  auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();
  callback({WorkerEvent::Diagnostic,"GpuWarmup ms="+std::to_string(elapsed)});
 }
 const auto baseline_drops=queue.dropped();
 CompletedSegmentQueue segments;
 std::atomic<bool> capture_done{false},capture_failed{false};
 std::atomic<uint64_t> converted_frames{0},completed{0};
 // This thread keeps consuming PCM while the inference thread is inside
 // whisper_full. Its only blocking lock protects completed phrase pointers;
 // OBS's audio callback never touches that lock or waits for inference.
 std::thread capture([&]{
  try{
   AudioResampler resampler;SpeechSegmenter segmenter;AudioBlock block;
   uint64_t generation=0,last_drops=baseline_drops,utterance=1;uint32_t rate=0;size_t channels=0;
   bool partial_started=false;
   while(!stopping && !capture_done){
    auto drops=queue.dropped();
    if(drops!=last_drops){callback({WorkerEvent::Status,"Overflow"});last_drops=drops;}
    if(!queue.pop(block)){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
    converted_frames.fetch_add(block.frames,std::memory_order_relaxed);
    if(block.discontinuity || block.generation!=generation || block.rate!=rate || block.channels!=channels){
     if(partial_started){
      if(!segments.finish_partial(utterance)){accepting=false;capture_failed=true;callback({WorkerEvent::Error,"BacklogFull"});break;}
     }else segments.cancel_partial(utterance);
     ++utterance;partial_started=false;
     resampler.reset();segmenter.reset();generation=block.generation;rate=block.rate;channels=block.channels;
    }
    auto phrase=segmenter.feed(resampler.convert(block));
    if(!phrase.empty()){
     if(!segments.push(std::move(phrase),utterance++)){
      accepting=false;capture_failed=true;callback({WorkerEvent::Error,"BacklogFull"});break;
     }
     ++completed;
     partial_started=false;
    }
    if(translate_to_english){auto snapshot=segmenter.take_partial();if(!snapshot.empty()){partial_started=true;segments.push_partial(std::move(snapshot),utterance);}}
   }
  }catch(const std::exception &e){accepting=false;capture_failed=true;callback({WorkerEvent::Error,e.what()});}
  catch(...){accepting=false;capture_failed=true;callback({WorkerEvent::Error,"CaptureFailed"});}
 });
 struct CaptureJoiner {
  std::thread &thread;std::atomic<bool> &done;
  ~CaptureJoiner(){done=true;if(thread.joinable())thread.join();}
 } joiner{capture,capture_done};
 const char *listening=translate_to_english?"ListeningEnglish":"Listening";
 const char *processing=translate_to_english?"Translating":"Recognizing";
 accepting=true;callback({WorkerEvent::Status,listening});
 callback({WorkerEvent::Diagnostic,translate_to_english?"Mode ru-to-en":"Mode ru-transcription"});
 uint64_t transcribed=0,partial_updates=0;
 auto diagnostics=[&](long long inference_ms,size_t phrase_samples){
  callback({WorkerEvent::Diagnostic,"AudioStats captured_frames="+std::to_string(captured_frames.load())+
   " converted_frames="+std::to_string(converted_frames.load())+
   " dropped_blocks="+std::to_string(queue.dropped()-baseline_drops)+
   " completed="+std::to_string(completed.load())+" transcribed="+std::to_string(transcribed)+
   " queued_samples="+std::to_string(segments.pending_samples())+
   " inference_audio_ms="+std::to_string(phrase_samples*1000/16000)+
   " inference_ms="+std::to_string(inference_ms)});
 };
 while(!stopping){
  RecognitionInput input;
  if(!segments.pop(input)){
   if(capture_failed)break;
   std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;
  }
  if(!input.final && !segments.current_partial(input.utterance))continue;
  if(input.final && input.pcm.empty()){callback({WorkerEvent::Text,"",input.utterance});continue;}
  if(input.final && !capture_failed)callback({WorkerEvent::Status,processing});
  auto params=inference_params();
  struct PartialAbort {std::atomic<bool> *stopping;CompletedSegmentQueue *segments;uint64_t utterance;};
  PartialAbort abort{&stopping,&segments,input.utterance};
  if(!input.final){
   params.abort_callback=[](void *p){auto *a=static_cast<PartialAbort *>(p);return a->stopping->load() || !a->segments->current_partial(a->utterance);};
   params.abort_callback_user_data=&abort;
  }
  auto before=std::chrono::steady_clock::now();
  int result=whisper_full(ctx.get(),params,input.pcm.data(),int(input.pcm.size()));
  auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();
  if(stopping)break;
  if(!input.final && !segments.current_partial(input.utterance))continue;
  if(result!=0){callback({WorkerEvent::Error,"InferenceFailed"});break;}
  if(input.final)++transcribed;
  std::string text;
  for(int i=0;i<whisper_full_n_segments(ctx.get());i++)text+=whisper_full_get_segment_text(ctx.get(),i);
  auto first=text.find_first_not_of(" \t\r\n");
  if(first!=std::string::npos){
   text=text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
   callback({input.final?WorkerEvent::Text:WorkerEvent::PartialText,std::move(text),input.utterance});
   if(!input.final)++partial_updates;
  }else if(input.final)callback({WorkerEvent::Text,"",input.utterance});
  if(input.final){
   diagnostics(elapsed,input.pcm.size());
   if(!capture_failed)callback({WorkerEvent::Status,listening});
  }else callback({WorkerEvent::Diagnostic,"PartialStats updates="+std::to_string(partial_updates)+
   " utterance="+std::to_string(input.utterance)+" audio_ms="+std::to_string(input.pcm.size()*1000/16000)+
   " inference_ms="+std::to_string(elapsed)});
 }
 accepting=false;capture_done=true;capture.join();
 diagnostics(0,0);
}
