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
};
class CompletedSegmentQueue {
public:
 static constexpr size_t max_samples=120*16000;
 bool push(std::vector<float> &&phrase,uint64_t utterance=0);
 void push_partial(std::vector<float> &&phrase,uint64_t utterance);
 void cancel_partial(uint64_t utterance);
 bool finish_partial(uint64_t utterance);
 bool current_partial(uint64_t utterance) const;
 bool pop(RecognitionInput &input);
 bool pop(std::vector<float> &phrase);
 size_t pending_samples() const;
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
};
class TranscriptionWorker {
public:
 using ResultCallback=std::function<void(WorkerEvent)>;
 ~TranscriptionWorker(){stop();}
 void start(const std::filesystem::path &model,unsigned threads,ResultCallback callback,bool translate_to_english=false,bool use_gpu=false);
 void stop();
 bool submit(const float *const planes[],size_t frames,size_t channels,uint32_t rate){
  if(!accepting.load(std::memory_order_acquire) || !queue.try_push(planes,frames,channels,rate))return false;
  captured_frames.fetch_add(frames,std::memory_order_relaxed);return true;
 }
private:
 void run(const std::filesystem::path &,unsigned,const ResultCallback &,bool translate_to_english,bool use_gpu);
 AudioBlockQueue queue;
 std::atomic<bool> stopping{true},accepting{false};
 std::atomic<uint64_t> captured_frames{0};
 std::thread thread;
};
