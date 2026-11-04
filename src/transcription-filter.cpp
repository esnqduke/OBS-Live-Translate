#include "browser-output.hpp"
#include "caption-layout.hpp"
#include "rolling-captions.hpp"
#include "caption-source.hpp"
#include "transcription-worker.hpp"
#include <obs-module.h>
#include <whisper.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <chrono>
#include <vector>

namespace {
// Pending UI tasks own only this mailbox, never a filter pointer. Closing or
// reconfiguring invalidates it, even if OBS has not yet executed the UI task.
struct Mailbox {
 std::mutex mutex;
 bool active=true,scheduled=false,failed=false,pending=false,clear_requested=false,partial=false;
 std::string resume_status="Loading";
 std::string target,status="Loading",text,visible,style,animation_text;
 size_t stable=0,revealed_bytes=0;
 bool restyle=false,muted=false;
 uint64_t generation=0;
 CaptionOptions options;
 int layout_rows=0,layout_font=0;
 int cyber_phase=0;
 std::chrono::steady_clock::time_point animation_start{},last_frame{};
 std::shared_ptr<obs_data_t> original_style;
 std::shared_ptr<CaptionLayout> layout;
 std::unique_ptr<RollingCaptions> rolling;
 std::deque<WorkerEvent> events;
 size_t event_bytes=0;
 bool output_taken=false,backlog_failed=false;
 int layout_width=0,layout_height=0,applied_size=64,applied_flags=1;
 std::string applied_face="Arial";
 std::shared_ptr<BrowserOutput> browser;
 std::string browser_config="{}",browser_image,owner,attached_url;
 bool target_resolved=false,browser_selected=false;
 std::chrono::steady_clock::time_point last_probe{};
 std::chrono::steady_clock::time_point expires{};
};
struct Filter {
 obs_source_t *source=nullptr;
 TranscriptionWorker worker;
 std::mutex lifecycle;
 std::shared_ptr<Mailbox> mailbox;
 std::string model,target,style;
 std::string browser_config,browser_image;
 unsigned threads=2;
 bool translate_to_english=false;
 bool use_gpu=false;
 obs_source_t *parent=nullptr;
 bool muted=false;
 CaptionOptions options;
 std::atomic<bool> enabled{false};
};
bool is_text(obs_source_t *source)
{
 return source && std::strcmp(obs_source_get_unversioned_id(source),"text_gdiplus")==0;
}
bool is_browser(obs_source_t *source){return source && std::strcmp(obs_source_get_unversioned_id(source),"browser_source")==0;}
bool browser_only_style(const std::string &style){return style=="matrix_rain" || style=="fantasy" || style=="cathay";}
const char *style_ints[]={"color","opacity","outline_size","outline_color","outline_opacity","bk_color","bk_opacity","text_transform"};
const char *style_bools[]={"outline","gradient","extents_wrap","chatlog_mode","vertical"};
obs_data_t *copy_style(obs_data_t *settings)
{
 auto *copy=obs_data_create();
 for(auto key:style_ints)obs_data_set_int(copy,key,obs_data_get_int(settings,key));
 for(auto key:style_bools)obs_data_set_bool(copy,key,obs_data_get_bool(settings,key));
 for(auto key:{"align","valign"})obs_data_set_string(copy,key,obs_data_get_string(settings,key));
 auto *font=obs_data_get_obj(settings,"font");
 if(font){obs_data_set_obj(copy,"font",font);obs_data_release(font);}
 return copy;
}
void decorate(obs_data_t *settings,const std::string &style,bool empty,double seconds)
{
 if(style.empty() || style=="plain" || browser_only_style(style))return;
 auto *font=obs_data_create();
 obs_data_set_string(font,"face",style=="cyber"?"Arial":"Consolas");
 obs_data_set_string(font,"style","Bold");obs_data_set_int(font,"flags",1);obs_data_set_int(font,"size",64);
 obs_data_set_obj(settings,"font",font);obs_data_release(font);
 obs_data_set_bool(settings,"gradient",false);obs_data_set_bool(settings,"outline",true);
 obs_data_set_int(settings,"opacity",100);obs_data_set_int(settings,"outline_opacity",100);
 obs_data_set_int(settings,"outline_size",style=="console"?2:4);
 obs_data_set_int(settings,"color",style=="matrix"?0xff66ff66:style=="console"?0xffddffdd:0xfffaffff);
 obs_data_set_int(settings,"outline_color",style=="cyber"?(seconds<0.35 && int(seconds*14)%2?0xffcc22ff:0xffffcc00):0xff082008);
 obs_data_set_int(settings,"bk_color",style=="cyber"?0xff201008:0xff080808);
 obs_data_set_int(settings,"bk_opacity",empty?0:style=="console"?72:style=="matrix"?42:55);
 obs_data_set_string(settings,"align",style=="console"?"left":"center");
}
std::string animation_frame(Mailbox &box,std::chrono::steady_clock::time_point now)
{
 const auto &text=box.animation_text;
 if(!box.options.animations || text.empty() || box.style.empty() || box.style=="plain" || browser_only_style(box.style)){box.revealed_bytes=text.size();return text;}
 double elapsed=std::max(0.0,std::chrono::duration<double>(now-box.animation_start).count())*box.options.animation_speed/100.0;
 std::vector<size_t> ends;
 for(size_t i=0;i<text.size();++i)if(i+1==text.size() || (static_cast<unsigned char>(text[i+1])&0xc0)!=0x80)ends.push_back(i+1);
 size_t prefix=0;for(auto end:ends)if(end<=box.stable)++prefix;
 size_t revealed=std::min(ends.size(),prefix+size_t(elapsed*std::max(45.0,double(ends.size()-prefix)/0.65)));
 size_t bytes=revealed?ends[revealed-1]:0;
 box.revealed_bytes=bytes;
 std::string result=text.substr(0,bytes);
 if(box.style=="console"){
  // Keep the cursor on the current line, within the existing extents.
  if(int(elapsed*2)%2==0 && (result.empty() || result.back()!='\n'))result+="_";
 }else if(revealed<ends.size()){
  static const char symbols[]="01#$%&<>[]";
  size_t count=std::min<size_t>(box.style=="matrix"?5:3,ends.size()-revealed);
  for(size_t i=0;i<count;++i)result+=symbols[(size_t(elapsed*30)+i*3)%10];
 }
 return result;
}
int64_t milliseconds(std::chrono::steady_clock::time_point now){return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();}
void render_caption(Mailbox &box,obs_source_t *target,std::chrono::steady_clock::time_point now)
{
 double seconds=std::chrono::duration<double>(now-box.animation_start).count()*box.options.animation_speed/100.0;
 if(!box.options.animations)seconds=10;
 auto frame=animation_frame(box,now);
 box.last_frame=now;
 int phase=box.style=="cyber" && seconds<0.35?int(seconds*14)%2:0;
 if(frame==box.visible && !box.restyle && phase==box.cyber_phase){if(box.rolling)box.rolling->mark_readable(box.revealed_bytes,milliseconds(now));return;}
 box.cyber_phase=phase;
 box.restyle=false;
 auto *current=obs_source_get_settings(target);
 if(!box.original_style && obs_data_get_bool(current,"live_translate_style_active")){
  auto *saved=obs_data_get_obj(current,"live_translate_original_style");
  if(saved)box.original_style={saved,obs_data_release};
 }
 if(!box.original_style && (box.rolling || box.options.font_size || box.options.alignment!="auto" || (box.style!="plain" && !box.style.empty())))box.original_style={copy_style(current),obs_data_release};
 obs_data_release(current);
 auto *settings=box.original_style?copy_style(box.original_style.get()):obs_data_create();
 if(box.original_style){
  obs_data_set_obj(settings,"live_translate_original_style",box.original_style.get());
  obs_data_set_bool(settings,"live_translate_style_active",box.rolling || box.options.font_size || box.options.alignment!="auto" || (box.style!="plain" && !box.style.empty()));
 }
 decorate(settings,box.style,frame.empty(),seconds);
 if(box.options.font_size)caption_apply_font(settings,box.options.font_size);
 if(box.options.alignment!="auto")obs_data_set_string(settings,"align",box.options.alignment.c_str());
 if(box.rolling){
  obs_data_set_string(settings,"valign","bottom");
  obs_data_set_bool(settings,"extents_wrap",false);obs_data_set_bool(settings,"chatlog_mode",false);
  obs_data_set_bool(settings,"vertical",false);obs_data_set_int(settings,"text_transform",0);
 }
 obs_data_set_bool(settings,"read_from_file",false);obs_data_set_string(settings,"text",frame.c_str());
 if(box.rolling){
  auto *font=obs_data_get_obj(settings,"font");
  if(font){box.applied_face=obs_data_get_string(font,"face");box.applied_size=int(obs_data_get_int(font,"size"));box.applied_flags=int(obs_data_get_int(font,"flags"));obs_data_release(font);}
 }
 obs_source_update(target,settings);obs_data_release(settings);box.visible=frame;box.last_frame=now;
 if(box.rolling)box.rolling->mark_readable(box.revealed_bytes,milliseconds(now));
 if(!box.rolling && !box.options.font_size && box.options.alignment=="auto" && (box.style=="plain" || box.style.empty()))box.original_style.reset();
}
void change_animation(Mailbox &box,const std::string &text,std::chrono::steady_clock::time_point now)
{
 if(text==box.animation_text)return;
 size_t common=0;
 while(common<box.animation_text.size() && common<text.size() && box.animation_text[common]==text[common])++common;
 box.stable=std::min(common,box.revealed_bytes);
 // After a scroll, retained rows form the prefix of the new window.
 for(size_t offset=0;(offset=box.animation_text.find('\n',offset))!=std::string::npos;){
  ++offset;auto retained=box.animation_text.substr(offset);
  if(text.compare(0,retained.size(),retained)==0 && box.revealed_bytes>=offset)
   box.stable=std::max(box.stable,std::min(retained.size(),box.revealed_bytes-offset));
 }
 while(box.stable && box.stable<text.size() && (static_cast<unsigned char>(text[box.stable])&0xc0)==0x80)--box.stable;
 if(box.stable<text.size() && text[box.stable]=='\n')++box.stable;
 box.animation_text=text;box.animation_start=now;
}
void update_roll(Mailbox &box,obs_source_t *target,std::chrono::steady_clock::time_point now)
{
 try{
  auto *settings=obs_source_get_settings(target);
  auto *current_font=obs_data_get_obj(settings,"font");
  std::string current_face=current_font?obs_data_get_string(current_font,"face"):"Arial";
  int current_size=current_font?int(obs_data_get_int(current_font,"size")):64;
  int current_flags=current_font?int(obs_data_get_int(current_font,"flags")):1;
  bool font_changed=box.layout && (current_face!=box.applied_face || current_size!=box.applied_size || current_flags!=box.applied_flags);
  int width=obs_data_get_bool(settings,"extents")?int(obs_data_get_int(settings,"extents_cx")):2304;
  int height=obs_data_get_bool(settings,"extents")?int(obs_data_get_int(settings,"extents_cy")):384;
  auto *saved=obs_data_get_obj(settings,"live_translate_original_style");
  auto *font=obs_data_get_obj(box.original_style?box.original_style.get():obs_data_get_bool(settings,"live_translate_style_active") && saved?saved:settings,"font");
  std::string face=font?obs_data_get_string(font,"face"):"Arial";
  int size=font?int(obs_data_get_int(font,"size")):64;
  int flags=font?int(obs_data_get_int(font,"flags")):1;
  if(font_changed){face=current_face;size=current_size;flags=current_flags;if(box.original_style && current_font)obs_data_set_obj(box.original_style.get(),"font",current_font);}
  if(font)obs_data_release(font);if(current_font)obs_data_release(current_font);if(saved)obs_data_release(saved);obs_data_release(settings);
  const int requested=box.options.font_size;
  const bool controls_changed=box.layout_rows!=box.options.max_rows || box.layout_font!=requested;
  if(requested)size=requested;
  if(!box.layout || width!=box.layout_width || height!=box.layout_height || font_changed || controls_changed){
   auto layout=std::make_shared<CaptionLayout>(face,size,flags,width,height,requested?requested:64);
   const auto capacity=std::min<size_t>(layout->rows(),box.options.max_rows);
   if(box.rolling){
    if(!box.rolling->reflow([layout](const std::string &value){return layout->wrap(value);},capacity,milliseconds(now)))throw std::runtime_error("Caption backlog exceeds resize budget");
    box.restyle=true;
   }else box.rolling=std::make_unique<RollingCaptions>(capacity,box.options.effect_ms(box.style));
   box.layout_rows=box.options.max_rows;box.layout_font=requested;
   box.layout=layout;box.layout_width=width;box.layout_height=height;
   box.applied_face=current_face;box.applied_size=current_size;box.applied_flags=current_flags;
  }
  box.rolling->configure(box.options,box.options.effect_ms(box.style),milliseconds(now));
  for(const auto &event:box.events){
   if(!box.rolling->update(event.utterance,box.layout->wrap(event.text),event.kind==WorkerEvent::Text,milliseconds(now),event.text)){
    box.status="CaptionBacklogFull";box.backlog_failed=true;
    blog(LOG_ERROR,"[obs-live-translate] CaptionBacklogFull");break;
   }
  }
  box.events.clear();box.event_bytes=0;
  box.rolling->advance(milliseconds(now));
  if(box.rolling->empty()){box.rolling.reset();box.layout.reset();return;}
  change_animation(box,box.rolling->text(),now);render_caption(box,target,now);
 }catch(const std::exception &error){
  box.status="CaptionLayoutFailed";box.backlog_failed=true;box.events.clear();box.event_bytes=0;
  blog(LOG_ERROR,"[obs-live-translate] CaptionLayoutFailed: %s",error.what());
 }
}
void publish(void *opaque);
void flush_browser(Mailbox &box){
 if(!box.browser)return;
 for(const auto &event:box.events)if(!box.browser->publish(event.utterance,event.text,event.kind==WorkerEvent::Text)){box.status="CaptionBacklogFull";box.backlog_failed=true;break;}
 box.events.clear();box.event_bytes=0;
 if(!box.text.empty())box.browser->preview(box.text);
 box.text.clear();box.pending=false;box.clear_requested=false;
}
void probe_browser(Mailbox &box){
 if(!box.active || box.target.empty() || box.output_taken)return;
 auto *target=obs_get_source_by_name(box.target.c_str());
 if(!target || obs_source_removed(target)){
  if(target)obs_source_release(target);box.target_resolved=false;
  if(box.browser){box.browser->deactivate();box.browser.reset();box.status="OutputMissing";box.output_taken=true;}return;
 }
 box.target_resolved=true;
 if(!is_browser(target)){obs_source_release(target);return;}
 box.browser_selected=true;auto *settings=obs_source_get_settings(target);
 std::string marker=obs_data_get_string(settings,"live_translate_browser_owner");
 if(marker!=box.owner || box.owner.empty()){
  if(box.browser){box.browser->deactivate();box.browser.reset();box.output_taken=true;}
  box.status="BrowserAttachNeeded";obs_data_release(settings);obs_source_release(target);return;
 }
 std::string url=obs_data_get_string(settings,"url");
 if(!box.attached_url.empty() && url!=box.attached_url){
  if(box.browser)box.browser->deactivate();box.browser.reset();box.output_taken=true;box.status="OutputChanged";
  obs_data_release(settings);obs_source_release(target);return;
 }
 try{
  if(!box.browser){
   box.browser=std::make_shared<BrowserOutput>(box.browser_config,box.browser_image);box.attached_url=box.browser->url();
   auto *changed=obs_data_create();obs_data_set_string(changed,"url",box.attached_url.c_str());
   obs_data_set_bool(changed,"is_local_file",false);obs_data_set_bool(changed,"shutdown",false);obs_data_set_bool(changed,"restart_when_active",false);
   obs_data_set_int(changed,"fps",30);obs_data_set_bool(changed,"fps_custom",true);
   obs_source_update(target,changed);obs_data_release(changed);
  }
  flush_browser(box);
 }catch(const std::exception &e){box.status="BrowserStartFailed";box.backlog_failed=true;blog(LOG_ERROR,"[obs-live-translate] BrowserStartFailed: %s",e.what());}
 obs_data_release(settings);obs_source_release(target);
}
void browser_probe_task(void *opaque){
 std::unique_ptr<std::shared_ptr<Mailbox>> payload(static_cast<std::shared_ptr<Mailbox> *>(opaque));auto box=*payload;
 bool schedule=false;
 {
  std::lock_guard<std::mutex> lock(box->mutex);box->scheduled=false;if(!box->active)return;
  probe_browser(*box);
  if(box->pending && !box->browser_selected){box->scheduled=true;schedule=true;}
 }
 if(schedule)obs_queue_task(OBS_TASK_UI,publish,new std::shared_ptr<Mailbox>(box),false);
}
void animate(void *opaque)
{
 std::unique_ptr<std::shared_ptr<Mailbox>> payload(static_cast<std::shared_ptr<Mailbox> *>(opaque));
 auto box=*payload;std::unique_lock<std::mutex> lock(box->mutex);
 box->scheduled=false;
 if(!box->active)return;
 if(box->pending || box->clear_requested){box->scheduled=true;lock.unlock();obs_queue_task(OBS_TASK_UI,publish,new std::shared_ptr<Mailbox>(box),false);return;}
 if(box->animation_text.empty())return;
 auto *target=obs_get_source_by_name(box->target.c_str());
 if(is_text(target) && !obs_source_removed(target)){
  auto *settings=obs_source_get_settings(target);
  bool owned=!obs_data_get_bool(settings,"read_from_file") && box->visible==obs_data_get_string(settings,"text");
  obs_data_release(settings);
  if(owned){if(box->rolling)update_roll(*box,target,std::chrono::steady_clock::now());else render_caption(*box,target,std::chrono::steady_clock::now());}
  else{box->animation_text.clear();box->rolling.reset();box->layout.reset();box->events.clear();box->event_bytes=0;box->output_taken=true;box->status="OutputChanged";}
 }
 if(target)obs_source_release(target);
}

// UI thread only, mailbox locked. Do not erase another writer's changed text.
void clear_owned(Mailbox &box)
{
 if(box.browser_selected){if(box.browser)box.browser->deactivate();box.browser.reset();box.events.clear();box.event_bytes=0;box.text.clear();box.pending=false;return;}
 if(box.visible.empty() && box.animation_text.empty() && !box.rolling && box.events.empty())return;
 auto *target=obs_get_source_by_name(box.target.c_str());
 if(is_text(target) && !obs_source_removed(target)){
  auto *settings=obs_source_get_settings(target);
  bool owned=!obs_data_get_bool(settings,"read_from_file") && box.visible==obs_data_get_string(settings,"text");
  obs_data_release(settings);
  if(owned){
   settings=box.original_style?copy_style(box.original_style.get()):obs_data_create();obs_data_set_string(settings,"text","");
   obs_data_set_bool(settings,"live_translate_style_active",false);
   obs_source_update(target,settings);obs_data_release(settings);
   blog(LOG_INFO,"[obs-live-translate] CaptionCleared");
  }
 }
 if(target)obs_source_release(target);
 box.visible.clear();box.animation_text.clear();box.original_style.reset();
 box.rolling.reset();box.layout.reset();box.events.clear();box.event_bytes=0;
}
void publish(void *opaque)
{
 std::unique_ptr<std::shared_ptr<Mailbox>> payload(static_cast<std::shared_ptr<Mailbox> *>(opaque));
 auto box=*payload;
 std::lock_guard<std::mutex> lock(box->mutex);
 box->scheduled=false;
 if(!box->active)return;
 if(!box->pending){
  if(box->clear_requested && !box->browser_selected)clear_owned(*box);
  box->clear_requested=false;return;
 }
 box->pending=false;box->clear_requested=false;
 obs_source_t *target=obs_get_source_by_name(box->target.c_str());
 if(is_browser(target) && !obs_source_removed(target)){obs_source_release(target);probe_browser(*box);return;}
 if(is_text(target))box->target_resolved=true;
 if(is_text(target) && !obs_source_removed(target)){
  auto now=std::chrono::steady_clock::now();
  if(!box->events.empty()){
   // Publishing is also an ownership boundary: do not reclaim externally edited text.
   auto *current=obs_source_get_settings(target);
   bool owned=!box->rolling || (!obs_data_get_bool(current,"read_from_file") && box->visible==obs_data_get_string(current,"text"));
   obs_data_release(current);
   if(owned)update_roll(*box,target,now);
   else{box->output_taken=true;box->status="OutputChanged";box->events.clear();box->event_bytes=0;box->rolling.reset();box->layout.reset();box->animation_text.clear();}
  }else{
  if(box->animation_text!=box->text){
   size_t common=0;
   while(common<box->animation_text.size() && common<box->text.size() && box->animation_text[common]==box->text[common])++common;
   while(common && common<box->text.size() && (static_cast<unsigned char>(box->text[common])&0xc0)==0x80)--common;
   box->stable=std::min(common,box->revealed_bytes);box->animation_start=now;box->animation_text=box->text;
  }
  render_caption(*box,target,now);
  const auto hold=size_t(box->options.reading(box->text)+box->options.effect_ms(box->style));
  box->expires=std::chrono::steady_clock::now()+std::chrono::milliseconds(hold);
  blog(LOG_INFO,"[obs-live-translate] CaptionPublished kind=%s hold_ms=%zu",box->partial?"partial":"final",hold);
  }
 }else{
  box->status="OutputMissing";
  blog(LOG_WARNING,"[obs-live-translate] Text (GDI+) output missing: %s",box->target.c_str());
 }
 if(target)obs_source_release(target);
 box->text.clear();
}
void expire(const std::shared_ptr<Mailbox> &box,std::chrono::steady_clock::time_point now)
{
 bool schedule=false;
 {
  std::unique_lock<std::mutex> lock(box->mutex,std::try_to_lock);
  if(!lock || !box->active || box->output_taken || box->visible.empty() || box->pending)return;
  if(box->rolling?!box->rolling->idle(milliseconds(now)):now<box->expires)return;
  box->clear_requested=true;
  if(!box->scheduled){box->scheduled=true;schedule=true;}
 }
 if(schedule)obs_queue_task(OBS_TASK_UI,publish,new std::shared_ptr<Mailbox>(box),false);
}
void post(const std::shared_ptr<Mailbox> &box,WorkerEvent event,TranscriptionWorker *worker=nullptr)
{
 if(event.kind==WorkerEvent::Diagnostic){blog(LOG_INFO,"[obs-live-translate] %s",event.text.c_str());return;}
 bool schedule=false;
 {
  std::lock_guard<std::mutex> lock(box->mutex);
  if(!box->active)return;
  if(event.kind==WorkerEvent::Status && box->muted){if(!box->failed)box->resume_status=event.text;return;}
  if(event.kind==WorkerEvent::Status && (box->failed || box->backlog_failed || box->output_taken))return;
  if(event.kind==WorkerEvent::Text || event.kind==WorkerEvent::PartialText){
   if(event.kind==WorkerEvent::PartialText && !box->options.partial_results)return;
   if(box->output_taken || box->backlog_failed || box->muted || (event.utterance && (event.generation!=box->generation || (worker && (worker->paused() || event.generation!=worker->generation())))))return;
   if(box->browser){
    if(event.utterance){if(!box->browser->publish(event.utterance,event.text,event.kind==WorkerEvent::Text)){box->status="CaptionBacklogFull";box->backlog_failed=true;}}
    else box->browser->preview(event.text);
    return;
   }
   if(event.utterance){
    bool replace=!box->events.empty() && box->events.back().utterance==event.utterance;
    if(replace && box->events.back().kind==WorkerEvent::Text)return;
    if(replace && event.kind==WorkerEvent::Text && event.text.empty())event.text=box->events.back().text;
    size_t old=replace?box->events.back().text.size():0;
    if(event.text.size()>RollingCaptions::max_bytes-(box->event_bytes-old) || (!replace && box->events.size()>=RollingCaptions::max_phrases)){
     box->status="CaptionBacklogFull";box->backlog_failed=true;blog(LOG_ERROR,"[obs-live-translate] CaptionBacklogFull");return;
    }
    box->event_bytes=box->event_bytes-old+event.text.size();
    if(replace)box->events.back()=std::move(event);else box->events.push_back(std::move(event));
   }else{
    if(box->rolling || !box->events.empty())return; // Preview never interrupts live reading.
    box->text=std::move(event.text);
   }
   box->pending=true;box->clear_requested=false;box->partial=event.kind==WorkerEvent::PartialText;
   if(!box->scheduled){box->scheduled=true;schedule=true;}
  }else{
   if(event.kind==WorkerEvent::Error)box->failed=true;
   box->status=event.text;
   blog(event.kind==WorkerEvent::Error?LOG_ERROR:LOG_INFO,"[obs-live-translate] %s",event.text.c_str());
  }
 }
 if(schedule)obs_queue_task(OBS_TASK_UI,publish,new std::shared_ptr<Mailbox>(box),false);
}
void stop(Filter *f)
{
 f->enabled=false;
 if(f->mailbox){
  auto box=f->mailbox;
  {std::lock_guard<std::mutex> lock(box->mutex);box->active=false;if(box->browser)box->browser->deactivate();}
  obs_queue_task(OBS_TASK_UI,[](void *p){
   std::unique_ptr<std::shared_ptr<Mailbox>> payload(static_cast<std::shared_ptr<Mailbox> *>(p));
   std::lock_guard<std::mutex> lock((*payload)->mutex);clear_owned(**payload);
  },new std::shared_ptr<Mailbox>(box),false);
 }
 f->worker.stop();
}
void start(Filter *f)
{
 auto box=std::make_shared<Mailbox>();box->target=f->target;box->style=f->style;box->owner=obs_source_get_uuid(f->source);box->browser_config=f->browser_config;box->browser_image=f->browser_image;f->mailbox=box;box->muted=f->muted;box->generation=f->worker.generation();box->options=f->options;
 if(!obs_source_enabled(f->source)){box->status="Stopped";return;}
 if(f->model.empty()){box->status="ModelMissing";return;}
 // Global Mic/Aux is loaded before the scene's text sources on OBS startup.
 // Resolve the selected name on the UI thread when publishing, not here.
 if(f->target.empty()){box->status="OutputMissing";return;}
 try{
  f->worker.start(std::filesystem::u8path(f->model),f->threads,[f,box](WorkerEvent e){post(box,std::move(e),&f->worker);},f->translate_to_english,f->use_gpu);
  f->enabled=true;
 }catch(const std::exception &e){box->status="StartFailed";blog(LOG_ERROR,"[obs-live-translate] %s",e.what());}
}
void update(void *opaque,obs_data_t *settings)
{
 auto *f=static_cast<Filter *>(opaque);std::lock_guard<std::mutex> lock(f->lifecycle);
 auto options=caption_options(settings);
 std::string style=browser_style(settings);std::string config=browser_config(settings),image=obs_data_get_string(settings,"background_image");
 if(f->mailbox && f->model==obs_data_get_string(settings,"model_path") && f->target==obs_data_get_string(settings,"output_source_name") && f->threads==unsigned(std::clamp<int64_t>(obs_data_get_int(settings,"cpu_threads"),1,32)) && f->translate_to_english==obs_data_get_bool(settings,"translate_to_english") && f->use_gpu==obs_data_get_bool(settings,"use_gpu") ){
  if(f->style==style && f->browser_config==config && f->browser_image==image)return;
  std::lock_guard<std::mutex> state(f->mailbox->mutex);f->style=style;f->mailbox->style=style;f->options=options;f->mailbox->options=options;
  f->browser_config=config;f->browser_image=image;f->mailbox->browser_config=config;f->mailbox->browser_image=image;
  if(f->mailbox->browser)f->mailbox->browser->configure(config,image);
  f->mailbox->animation_start=std::chrono::steady_clock::now();f->mailbox->stable=f->mailbox->rolling?f->mailbox->animation_text.size():0;f->mailbox->restyle=true;return;
 }
 stop(f);f->style=style;f->options=options;
 f->browser_config=config;f->browser_image=image;
 f->model=obs_data_get_string(settings,"model_path");
 f->target=obs_data_get_string(settings,"output_source_name");
 f->threads=unsigned(std::clamp<int64_t>(obs_data_get_int(settings,"cpu_threads"),1,32));
 f->translate_to_english=obs_data_get_bool(settings,"translate_to_english");
 f->use_gpu=obs_data_get_bool(settings,"use_gpu");
 start(f);
}
bool connect_browser(Filter *f){
 if(!f)return false;std::lock_guard<std::mutex> lifecycle(f->lifecycle);if(!f->mailbox)return false;
 auto box=f->mailbox;std::lock_guard<std::mutex> lock(box->mutex);
 auto *target=obs_get_source_by_name(box->target.c_str());
 if(!is_browser(target) || obs_source_removed(target)){if(target)obs_source_release(target);box->status="BrowserSelectOutput";return false;}
 if(box->browser){auto *current=obs_source_get_settings(target);bool attached=box->attached_url==obs_data_get_string(current,"url") && box->owner==obs_data_get_string(current,"live_translate_browser_owner");obs_data_release(current);if(attached){obs_source_release(target);return true;}}
 auto *settings=obs_data_create();obs_data_set_string(settings,"live_translate_browser_owner",box->owner.c_str());obs_source_update(target,settings);obs_data_release(settings);obs_source_release(target);
 box->output_taken=false;box->backlog_failed=false;box->attached_url.clear();if(box->browser)box->browser->deactivate();box->browser.reset();
 probe_browser(*box);return bool(box->browser);
}
void reset_captions(Filter *f,bool muted)
{
 auto box=f->mailbox;if(!box)return;
 bool schedule=false;
 {
  std::lock_guard<std::mutex> state(box->mutex);
  box->generation=f->worker.reset_audio();box->muted=muted;
  box->events.clear();box->event_bytes=0;box->text.clear();box->pending=false;
  box->rolling.reset();box->layout.reset();box->clear_requested=true;
  if(box->browser)box->browser->clear();
  if(muted){if(box->status!="MicrophoneMuted")box->resume_status=box->status;box->status="MicrophoneMuted";}
  if(!box->scheduled){box->scheduled=true;schedule=true;}
 }
 // OBS may execute a UI task inline, even with wait=false. Never dispatch while
 // holding the mailbox mutex: publish takes the same mutex.
 if(schedule)obs_queue_task(OBS_TASK_UI,publish,new std::shared_ptr<Mailbox>(box),false);
}
void sync_mute(Filter *f,bool muted)
{
 const auto token=f->worker.set_paused(muted);const bool changed=f->muted!=muted;
 bool drift=false;if(f->mailbox){std::lock_guard<std::mutex> state(f->mailbox->mutex);drift=f->mailbox->generation!=token;}
 if(!changed && !drift)return;
 f->muted=muted;reset_captions(f,muted);
 if(changed && !muted && f->mailbox){std::lock_guard<std::mutex> state(f->mailbox->mutex);if(!f->mailbox->failed){auto status=f->mailbox->resume_status;if(status=="Recognizing" || status=="Translating")status=f->translate_to_english?"ListeningEnglish":"Listening";f->mailbox->status=status;}}
}
void parent_mute(void *opaque,calldata_t *data)
{
 auto *f=static_cast<Filter *>(opaque);std::lock_guard<std::mutex> lock(f->lifecycle);sync_mute(f,calldata_bool(data,"muted"));
}
void filter_remove(void *opaque,obs_source_t *)
{
 auto *f=static_cast<Filter *>(opaque);
 if(f->parent){signal_handler_disconnect(obs_source_get_signal_handler(f->parent),"mute",parent_mute,f);f->parent=nullptr;}
}
void filter_add(void *opaque,obs_source_t *parent)
{
 auto *f=static_cast<Filter *>(opaque);filter_remove(f,nullptr);f->parent=parent;
 signal_handler_connect(obs_source_get_signal_handler(parent),"mute",parent_mute,f);
 std::lock_guard<std::mutex> lock(f->lifecycle);sync_mute(f,obs_source_muted(parent));
}
void enable(void *opaque,calldata_t *data)
{
 auto *f=static_cast<Filter *>(opaque);std::lock_guard<std::mutex> lock(f->lifecycle);
 stop(f);
 if(calldata_bool(data,"enabled"))start(f);
 else{f->mailbox=std::make_shared<Mailbox>();f->mailbox->status="Stopped";}
}
void *create(obs_data_t *settings,obs_source_t *source)
{
 auto f=std::make_unique<Filter>();f->source=source;
 update(f.get(),settings);
 signal_handler_connect(obs_source_get_signal_handler(source),"enable",enable,f.get());
 return f.release();
}
void destroy(void *opaque)
{
 auto *f=static_cast<Filter *>(opaque);
 filter_remove(f,nullptr);
 signal_handler_disconnect(obs_source_get_signal_handler(f->source),"enable",enable,f);
 {std::lock_guard<std::mutex> lock(f->lifecycle);stop(f);}
 delete f;
}
obs_audio_data *audio(void *opaque,obs_audio_data *data)
{
 auto *f=static_cast<Filter *>(opaque);
 auto *parent=obs_filter_get_parent(f->source);
 if(parent && obs_source_muted(parent))f->worker.set_paused(true);
 if(!data || !f->enabled.load() || f->worker.paused())return data;
 obs_audio_info info{};
 if(obs_get_audio_info(&info)){
  const float *planes[MAX_AV_PLANES]{};
  const size_t channels=get_audio_channels(info.speakers);
  if(channels>AudioBlock::max_channels)return data;
  for(size_t c=0;c<channels;c++)planes[c]=reinterpret_cast<const float *>(data->data[c]);
  f->worker.submit(planes,data->frames,channels,info.samples_per_sec);
 }
 return data;
}
void tick(void *opaque,float)
{
 auto *f=static_cast<Filter *>(opaque);
 std::unique_lock<std::mutex> lock(f->lifecycle,std::try_to_lock);
 if(!lock || !f->mailbox)return;
 auto *parent=obs_filter_get_parent(f->source);sync_mute(f,parent && obs_source_muted(parent));
 auto box=f->mailbox;lock.unlock();auto now=std::chrono::steady_clock::now();expire(box,now);
 bool schedule=false;
 {std::unique_lock<std::mutex> state(box->mutex,std::try_to_lock);
  if(state && !box->target.empty() && !box->output_taken && box->active && !box->scheduled && (!box->target_resolved || box->browser_selected) && now-box->last_probe>=std::chrono::milliseconds(500)){
   box->scheduled=true;box->last_probe=now;state.unlock();obs_queue_task(OBS_TASK_UI,browser_probe_task,new std::shared_ptr<Mailbox>(box),false);return;
  }
  if(state && !box->output_taken && !box->browser_selected && (box->rolling || box->restyle || box->style=="console" || box->revealed_bytes<box->animation_text.size() || box->cyber_phase!=0) && box->active && !box->scheduled && !box->animation_text.empty() && (box->rolling || now<box->expires) && now-box->last_frame>=std::chrono::milliseconds(50)){
   // One final frame also restores the original appearance when switching to Plain.
   box->scheduled=true;schedule=true;
  }
 }
 if(schedule)obs_queue_task(OBS_TASK_UI,animate,new std::shared_ptr<Mailbox>(box),false);
}
bool enumerate(void *opaque,obs_source_t *source)
{
 if(is_text(source) || is_browser(source)){
  std::string label=std::string(obs_source_get_name(source))+(is_browser(source)?" [Browser]":" [GDI+]");
  obs_property_list_add_string(static_cast<obs_property_t *>(opaque),label.c_str(),obs_source_get_name(source));
 }
 return true;
}
void refresh(obs_properties_t *p,Filter *f)
{
 auto *list=obs_properties_get(p,"output_source_name");
 obs_property_list_clear(list);
 obs_property_list_add_string(list,obs_module_text("SelectOutput"),"");
 obs_enum_sources(enumerate,list);
 std::string status="Stopped";
 if(f){std::lock_guard<std::mutex> lock(f->lifecycle);
  if(f->mailbox){std::lock_guard<std::mutex> state(f->mailbox->mutex);status=f->mailbox->status;}}
 obs_property_set_description(obs_properties_get(p,"status"),obs_module_text(status.c_str()));
 std::string browser_status="BrowserInfo";
 if(f){std::lock_guard<std::mutex> lock(f->lifecycle);if(f->mailbox){std::lock_guard<std::mutex> state(f->mailbox->mutex);
  if(f->mailbox->browser)browser_status=f->mailbox->browser->connected()?"BrowserConnected":"BrowserWaiting";
  else if(f->mailbox->browser_selected)browser_status="BrowserAttachNeeded";
 }}
 obs_property_set_description(obs_properties_get(p,"browser_status"),obs_module_text(browser_status.c_str()));
}
obs_properties_t *properties(void *opaque)
{
 auto *p=obs_properties_create();
 obs_properties_add_path(p,"model_path",obs_module_text("ModelPath"),OBS_PATH_FILE,"Whisper (*.bin);;All files (*.*)",nullptr);
 auto *list=obs_properties_add_list(p,"output_source_name",obs_module_text("OutputSource"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
 obs_property_list_add_string(list,obs_module_text("SelectOutput"),"");obs_enum_sources(enumerate,list);
 for(const auto &n:caption_numbers)obs_properties_add_int(p,n.setting,obs_module_text(n.label),n.min,n.max,n.step);
 obs_properties_add_bool(p,"caption_animations",obs_module_text("CaptionAnimations"));
 obs_properties_add_bool(p,"caption_partial_results",obs_module_text("CaptionPartialResults"));
 auto *alignment=obs_properties_add_list(p,"caption_alignment",obs_module_text("CaptionAlignment"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
 for(auto pair:{std::pair<const char *,const char *>{"CaptionAlignAuto","auto"},{"CaptionAlignLeft","left"},{"CaptionAlignCenter","center"},{"CaptionAlignRight","right"}})obs_property_list_add_string(alignment,obs_module_text(pair.first),pair.second);
 obs_properties_add_text(p,"caption_controls_info",obs_module_text("CaptionControlsInfo"),OBS_TEXT_INFO);
 obs_properties_add_button(p,"clear_captions",obs_module_text("ClearCaptions"),[](obs_properties_t *,obs_property_t *,void *opaque){auto *f=static_cast<Filter *>(opaque);if(f){std::lock_guard<std::mutex> lock(f->lifecycle);reset_captions(f,f->muted);}return false;});
 obs_properties_add_button(p,"reset_captions",obs_module_text("ResetCaptionSettings"),[](obs_properties_t *,obs_property_t *,void *opaque){auto *f=static_cast<Filter *>(opaque);if(!f)return false;auto *s=obs_source_get_settings(f->source);caption_defaults(s,true);obs_source_update(f->source,s);obs_data_release(s);return true;});
 auto *styles=obs_properties_add_list(p,"caption_style",obs_module_text("CaptionStyle"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
 for(auto pair:{std::pair<const char *,const char *>{"StylePlain","plain"},{"StyleMatrix","matrix"},{"StyleConsole","console"},{"StyleCyber","cyber"},{"StyleMatrixRain","matrix_rain"},{"StyleFantasy","fantasy"},{"StyleCathay","cathay"}})obs_property_list_add_string(styles,obs_module_text(pair.first),pair.second);
 obs_properties_add_button(p,"connect_browser",obs_module_text("ConnectBrowser"),[](obs_properties_t *,obs_property_t *,void *data){connect_browser(static_cast<Filter *>(data));return true;});
 obs_properties_add_text(p,"browser_status",obs_module_text("BrowserInfo"),OBS_TEXT_INFO);
 auto *backgrounds=obs_properties_add_list(p,"background_type",obs_module_text("BackgroundType"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
 for(auto pair:{std::pair<const char *,const char *>{"BackgroundAuto","auto"},{"BackgroundNone","none"},{"BackgroundSolid","solid"},{"BackgroundGradient","gradient"},{"BackgroundParchment","parchment"},{"BackgroundCathay","cathay"},{"BackgroundImage","image"}})obs_property_list_add_string(backgrounds,obs_module_text(pair.first),pair.second);
 obs_properties_add_bool(p,"background_custom",obs_module_text("BackgroundCustom"));
 obs_properties_add_color(p,"background_color",obs_module_text("BackgroundColor"));obs_properties_add_color(p,"background_color2",obs_module_text("BackgroundColor2"));
 obs_properties_add_int(p,"background_opacity",obs_module_text("BackgroundOpacity"),0,100,1);
 obs_properties_add_int(p,"background_paddingX",obs_module_text("BackgroundPaddingX"),0,256,1);obs_properties_add_int(p,"background_paddingY",obs_module_text("BackgroundPaddingY"),0,128,1);
 obs_properties_add_int(p,"background_radius",obs_module_text("BackgroundRadius"),0,128,1);obs_properties_add_int(p,"background_borderWidth",obs_module_text("BackgroundBorderWidth"),0,32,1);
 obs_properties_add_color(p,"background_border_color",obs_module_text("BackgroundBorderColor"));
 obs_properties_add_path(p,"background_image",obs_module_text("BackgroundImageFile"),OBS_PATH_FILE,"Images (*.png *.jpg *.jpeg *.webp)",nullptr);
 auto *fit=obs_properties_add_list(p,"background_image_fit",obs_module_text("BackgroundImageFit"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
 obs_property_list_add_string(fit,obs_module_text("BackgroundCover"),"cover");obs_property_list_add_string(fit,obs_module_text("BackgroundContain"),"contain");
 obs_properties_add_button(p,"reset_background",obs_module_text("ResetBackground"),[](obs_properties_t *,obs_property_t *,void *opaque){
  auto *f=static_cast<Filter *>(opaque);if(!f)return false;auto *settings=obs_source_get_settings(f->source);
  obs_data_set_string(settings,"background_type","auto");obs_data_set_bool(settings,"background_custom",false);obs_source_update(f->source,settings);obs_data_release(settings);return true;
 });
 obs_properties_add_int(p,"cpu_threads",obs_module_text("CpuThreads"),1,32,1);
 obs_properties_add_bool(p,"translate_to_english",obs_module_text("TranslateToEnglish"));
 obs_properties_add_bool(p,"use_gpu",obs_module_text("UseGpu"));
 obs_properties_add_button(p,"preview_style",obs_module_text("PreviewStyle"),[](obs_properties_t *,obs_property_t *,void *opaque){
  auto *f=static_cast<Filter *>(opaque);if(!f)return false;
  std::shared_ptr<Mailbox> box;{std::lock_guard<std::mutex> lock(f->lifecycle);box=f->mailbox;}
  if(box)post(box,{WorkerEvent::Text,"Live subtitles. Choose your style."});return false;
 });
 obs_properties_add_text(p,"status",obs_module_text("Status"),OBS_TEXT_INFO);
 obs_properties_add_button(p,"refresh",obs_module_text("Refresh"),[](obs_properties_t *props,obs_property_t *,void *data){refresh(props,static_cast<Filter *>(data));return true;});
 refresh(p,static_cast<Filter *>(opaque));
 return p;
}
}
extern "C" void register_transcription_source(void)
{
 whisper_log_set([](ggml_log_level level,const char *text,void *){
  blog(level==GGML_LOG_LEVEL_ERROR?LOG_ERROR:LOG_INFO,"[obs-live-translate/whisper] %s",text);
 },nullptr);
 obs_source_info info{};
 info.id="obs_live_translate_transcription_filter";info.type=OBS_SOURCE_TYPE_FILTER;info.output_flags=OBS_SOURCE_AUDIO;
 info.get_name=[](void *){return obs_module_text("TranscriptionFilter");};
 info.create=create;info.destroy=destroy;info.update=update;info.filter_audio=audio;info.get_properties=properties;
 info.video_tick=tick;info.filter_add=filter_add;info.filter_remove=filter_remove;
 info.get_defaults=[](obs_data_t *s){
  caption_defaults(s);
  obs_data_set_default_string(s,"caption_style","plain");obs_data_set_default_int(s,"cpu_threads",2);obs_data_set_default_bool(s,"translate_to_english",false);obs_data_set_default_bool(s,"use_gpu",false);
  obs_data_set_default_string(s,"background_type","auto");obs_data_set_default_bool(s,"background_custom",false);obs_data_set_default_string(s,"background_image_fit","cover");
  for(auto pair:{std::pair<const char *,int64_t>{"background_color",0x0f1108},{"background_color2",0x2c301d},{"background_border_color",0x6ba4c5},{"background_opacity",72},{"background_paddingX",32},{"background_paddingY",24},{"background_radius",24},{"background_borderWidth",2}})obs_data_set_default_int(s,pair.first,pair.second);
 };
 obs_register_source(&info);
}
extern "C" void unregister_transcription_logging(void)
{
 whisper_log_set(nullptr,nullptr);
}
