#include "../src/transcription-filter.cpp"
#include <iostream>
#include <stdexcept>
void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
int main(){try{
 register_transcription_source();inline_ui_tasks=true;
 obs_source_t filter_source,parent;filter_source.parent=&parent;parent.muted=true;
 obs_data_t settings;registered.get_defaults(&settings);obs_data_set_string(&settings,"output_source_name","captions");
 auto *f=static_cast<Filter *>(registered.create(&settings,&filter_source));
 std::cout<<"startup with muted parent and immediate UI dispatch"<<std::endl;
 registered.filter_add(f,&parent);
 check(f->mailbox->muted && f->worker.paused() && !f->mailbox->scheduled,"startup mute completes immediate cleanup");
 parent.muted=false;calldata_t unmuted{false};parent_mute(f,&unmuted);
 auto box=f->mailbox;post(box,{WorkerEvent::Text,"visible after unmute",1,f->worker.generation()});
 check(written=="visible after unmute","immediate publisher renders fresh caption");
 reset_captions(f,false);check(written.empty(),"manual clear can execute synchronously");
 {std::lock_guard<std::mutex> lock(box->mutex);box->text="probe pending caption";box->pending=true;box->target_resolved=false;}
 std::cout<<"probe and nested publisher with immediate dispatch"<<std::endl;
 registered.video_tick(f,0.05f);check(written=="probe pending caption","probe releases mailbox before nested publish");
 {std::lock_guard<std::mutex> lock(box->mutex);box->text="animation pending caption";box->pending=true;box->scheduled=true;}
 animate(new std::shared_ptr<Mailbox>(box));check(written=="animation pending caption","animation releases mailbox before nested publish");
 {std::lock_guard<std::mutex> lock(box->mutex);box->clear_requested=true;box->scheduled=true;}
 animate(new std::shared_ptr<Mailbox>(box));check(written.empty(),"animation can immediately hand off clear");
 parent.muted=true;calldata_t muted{true};parent_mute(f,&muted);
 check(box->muted && box->generation==f->worker.generation(),"UI mute keeps generation synchronized");
 registered.destroy(f);check(tasks.empty() && references==0,"immediate dispatch destroys safely and releases references");
 std::cout<<"immediate UI: muted startup, unmute, clear, probe, animation and destroy: PASS\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
