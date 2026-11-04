#include "audio-resampler.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

bool AudioBlockQueue::try_push(const float *const planes[],size_t frames,size_t channels,uint32_t rate,uint64_t capture_generation)
{
 if(!planes || !frames || frames>AudioBlock::max_frames || !channels ||
    channels>AudioBlock::max_channels || rate<8000 || rate>192000) return false;
 for(size_t c=0;c<channels;c++) if(!planes[c])return false;
 const uint64_t sequence=next_sequence.fetch_add(1);
 const uint64_t minimum=minimum_sequence.load();
 Slot *chosen=nullptr;uint64_t expected=0,oldest=std::numeric_limits<uint64_t>::max();
 for(size_t i=0;i<capacity;i++){
  auto tag=slots[i].tag.load(std::memory_order_acquire);
  if((tag&3)==0 || ((tag&3)==2 && (tag>>2)<minimum)){chosen=&slots[i];expected=tag;break;}
  if((tag&3)==2 && (tag>>2)<oldest){oldest=tag>>2;chosen=&slots[i];expected=tag;}
 }
 if(!chosen || !chosen->tag.compare_exchange_strong(expected,(sequence<<2)|1,std::memory_order_acq_rel)){++drops;return false;}
 if((expected&3)==2 && (expected>>2)>=minimum)++drops;
 auto &b=chosen->block;
 b.frames=frames;b.channels=channels;b.rate=rate;b.capture_generation=capture_generation;b.generation=drops.load();b.discontinuity=false;
 for(size_t c=0;c<channels;c++)std::copy_n(planes[c],frames,b.samples.data()+c*AudioBlock::max_frames);
 chosen->tag.store((sequence<<2)|2,std::memory_order_release);return true;
}
bool AudioBlockQueue::pop(AudioBlock &out)
{
 Slot *chosen=nullptr;uint64_t expected=0,oldest=std::numeric_limits<uint64_t>::max();
 const auto minimum=minimum_sequence.load();
 // Ignore publications that began after this scan. Otherwise a slow scan
 // could miss an older publication in an already visited slot and select a
 // newer one farther along the array. A prior in-flight write also gates pop.
 const auto upper=next_sequence.load();
 for(size_t i=0;i<capacity;i++){
  auto tag=slots[i].tag.load(std::memory_order_acquire);
  if((tag&3)!=2 && (tag&3)!=1)continue;
  if((tag>>2)<minimum){if((tag&3)==2)slots[i].tag.compare_exchange_strong(tag,0);continue;}
  if((tag>>2)>=upper)continue;
  if((tag>>2)<oldest){oldest=tag>>2;chosen=&slots[i];expected=tag;}
 }
 if(!chosen || (expected&3)!=2 || !chosen->tag.compare_exchange_strong(expected,(oldest<<2)|3,std::memory_order_acq_rel))return false;
 std::swap(out,chosen->block);
 out.discontinuity=oldest!=last_sequence+1;last_sequence=oldest;
 chosen->tag.store(0,std::memory_order_release);return true;
}
void AudioBlockQueue::clear(){minimum_sequence=next_sequence.load();last_sequence=0;}
void AudioResampler::reset(){rate=0;input_count=output_count=0;previous=0;}
std::vector<float> AudioResampler::convert(const AudioBlock &b)
{
 if(!b.frames || b.frames>AudioBlock::max_frames || !b.channels || b.channels>AudioBlock::max_channels || b.rate<8000 || b.rate>192000)return {};
 if(rate!=b.rate){reset();rate=b.rate;}
 std::vector<float> out;
 out.reserve(b.frames*16000/rate+2);
 for(size_t i=0;i<b.frames;i++){
  float sample=0;
  for(size_t c=0;c<b.channels;c++){
   float v=b.samples[c*AudioBlock::max_frames+i];
   sample+=std::isfinite(v)?std::clamp(v,-1.0f,1.0f):0.0f;
  }
  sample/=float(b.channels);
  // Streaming linear interpolation retains fractional position across blocks.
  while(output_count*uint64_t(rate)<=input_count*16000){
   double position=double(output_count)*rate/16000.0;
   float fraction=float(position-(double(input_count)-1.0));
   out.push_back(input_count?previous+(sample-previous)*fraction:sample);
   ++output_count;
  }
  previous=sample;++input_count;
 }
 return out;
}
void SpeechSegmenter::reset(){window.clear();segment.clear();voiced=silent=last_partial=0;}
std::vector<float> SpeechSegmenter::take_partial()
{
 if(silent || voiced<16000 || segment.size()<last_partial+16000)return {};
 last_partial=segment.size();return segment;
}
std::vector<float> SpeechSegmenter::feed(const std::vector<float> &pcm)
{
 std::vector<float> result;
 for(float v:pcm){
  window.push_back(v);if(window.size()<160)continue;
  double energy=0;for(float x:window)energy+=x*x;
  bool speech=energy/window.size()>=threshold*threshold;
  if(speech || !segment.empty()){
   segment.insert(segment.end(),window.begin(),window.end());
   if(speech){voiced+=160;silent=0;}else silent+=160;
   if(silent>=11200 || segment.size()>=240000){
    if(voiced>=4800)result=std::move(segment);
    segment.clear();voiced=silent=last_partial=0;
   }
  }
  window.clear();
 }
 return result;
}
