#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>

struct AudioBlock {
 static constexpr size_t max_frames=2048, max_channels=8;
 std::vector<float> samples=std::vector<float>(max_frames*max_channels);
 size_t frames=0,channels=0;
 uint32_t rate=0;
 uint64_t generation=0,capture_generation=0;
 bool discontinuity=false;
};
class AudioBlockQueue {
public:
 static_assert(std::atomic<uint64_t>::is_always_lock_free,"Audio queue requires lock-free 64-bit atomics");
 static constexpr size_t capacity=64;
 bool try_push(const float *const planes[],size_t frames,size_t channels,uint32_t rate,uint64_t capture_generation=0);
 bool pop(AudioBlock &out);
 void clear();
 uint64_t dropped() const {return drops.load();}
private:
 struct Slot {
  // Atomic tag contains a sequence number and ownership: free=0, writing=1,
  // ready=2, reading=3. Exact CAS tags prevent reuse/ABA during scans.
  std::atomic<uint64_t> tag{0};
  AudioBlock block;
 };
 std::unique_ptr<Slot[]> slots=std::make_unique<Slot[]>(capacity);
 std::atomic<uint64_t> next_sequence{1},minimum_sequence{1};
 uint64_t last_sequence=0; // consumer only
 std::atomic<uint64_t> drops{0};
};
class AudioResampler {
public:
 std::vector<float> convert(const AudioBlock &block);
 void reset();
private:
 uint32_t rate=0;
 uint64_t input_count=0, output_count=0;
 float previous=0;
};
class SpeechSegmenter {
public:
 // 10 ms RMS windows, -40 dBFS gate, 700 ms pause, 300 ms minimum speech,
 // 15 second maximum. This is an energy gate, not a linguistic VAD.
 static constexpr float threshold=0.01f;
 std::vector<float> feed(const std::vector<float> &pcm);
 std::vector<float> take_partial();
 void reset();
private:
 std::vector<float> window,segment;
 size_t voiced=0,silent=0,last_partial=0;
};
