#pragma once
#include "audio-resampler.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <deque>

struct RecognitionInput {
 std::vector<float> pcm;
 uint64_t utterance=0;
 bool final=true;
 uint64_t generation=0;
};
class CompletedSegmentQueue {
public:
 static constexpr size_t max_samples=120*16000;
 bool push(std::vector<float> &&phrase,uint64_t utterance=0,uint64_t generation=0);
 void push_partial(std::vector<float> &&phrase,uint64_t utterance,uint64_t generation=0);
 void cancel_partial(uint64_t utterance);
 bool finish_partial(uint64_t utterance,uint64_t generation=0);
 bool current_partial(uint64_t utterance) const;
 bool pop(RecognitionInput &input);
 bool pop(std::vector<float> &phrase);
 size_t pending_samples() const;
 void clear(uint64_t through=0);
private:
 mutable std::mutex mutex;
 std::deque<RecognitionInput> phrases;
 RecognitionInput partial;
 uint64_t finalized=0;
 size_t samples=0;
};

struct WorkerEvent {
 enum Kind {Text,Status,Error,Diagnostic,PartialText} kind;
 std::string text;
 uint64_t utterance=0; // Zero is reserved for unsequenced UI preview/status events.
 uint64_t generation=0;
};
class TranscriptionWorker {
public:
 using ResultCallback=std::function<void(WorkerEvent)>;
 ~TranscriptionWorker(){stop();}
 void start(const std::filesystem::path &model,unsigned threads,ResultCallback callback,bool translate_to_english=false,bool use_gpu=false);
 void stop();
 // One atomic token changes even across a rapid mute/unmute pair. Low bit is pause.
 uint64_t generation() const{return control.load(std::memory_order_acquire);}
 bool paused() const{return (generation()&1)!=0;}
 uint64_t reset_audio(){return control.fetch_add(2,std::memory_order_acq_rel)+2;}
 uint64_t set_paused(bool pause){
  auto old=control.load();
  while(bool(old&1)!=pause){auto next=((old+2)&~uint64_t(1))|uint64_t(pause);if(control.compare_exchange_weak(old,next))return next;}
  return old;
 }
 bool submit(const float *const planes[],size_t frames,size_t channels,uint32_t rate){
  const auto token=generation();
  if((token&1) || !accepting.load(std::memory_order_acquire) || !queue.try_push(planes,frames,channels,rate,token))return false;
  captured_frames.fetch_add(frames,std::memory_order_relaxed);return true;
 }
private:
 void run(const std::filesystem::path &,unsigned,const ResultCallback &,bool translate_to_english,bool use_gpu);
 AudioBlockQueue queue;
 std::atomic<bool> stopping{true},accepting{false};
 std::atomic<uint64_t> captured_frames{0},control{0};
 std::thread thread;
};
