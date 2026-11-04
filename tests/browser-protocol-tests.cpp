// Uses real libobs JSON rather than the filter lifecycle test double.
#include "../src/browser-output.hpp"
#include <iostream>
#include <stdexcept>
void check(bool b,const char *message){if(!b)throw std::runtime_error(message);}
int main(int argc,char **argv){try{
 BrowserJournal journal("trusted");journal.publish(1,"draft",false);journal.publish(1,"final",true);
 check(!browser_acknowledge(journal,"trusted","bad"),"malformed JSON rejected");
 check(!browser_acknowledge(journal,"trusted","{\"session\":\"other\",\"ids\":[{\"id\":\"1\",\"revision\":2}]}"),"wrong session cannot acknowledge");
 check(!browser_acknowledge(journal,"trusted","{\"session\":\"trusted\"}"),"missing list rejected");
 check(!browser_acknowledge(journal,"trusted","{\"session\":\"trusted\",\"ids\":[{\"id\":\"18446744073709551616\",\"revision\":2}]}"),"overflowing phrase ID rejected");
 check(browser_acknowledge(journal,"trusted","{\"session\":\"trusted\",\"ids\":[{\"id\":\"1\",\"revision\":1}]}"),"well-formed stale ack harmless");
 check(journal.snapshot().size()==1,"stale ack retains final");
 check(browser_acknowledge(journal,"trusted","{\"session\":\"trusted\",\"ids\":[{\"id\":\"1\",\"revision\":2}]}"),"exact final revision acknowledged");
 check(journal.snapshot().empty(),"only exact final ack releases journal");
 auto *settings=obs_data_create();obs_data_set_string(settings,"caption_style","cathay");obs_data_set_string(settings,"background_type","gradient");
 obs_data_set_bool(settings,"background_custom",true);obs_data_set_int(settings,"background_opacity",999);
 auto config=browser_config(settings);auto *decoded=obs_data_create_from_json(config.c_str());
 check(decoded && std::string(obs_data_get_string(decoded,"style"))=="cathay" && obs_data_get_int(decoded,"opacity")==100,"real libobs config JSON normalized");
 obs_data_release(decoded);obs_data_release(settings);
 auto *original=obs_data_create();auto *original_font=obs_data_create();obs_data_set_string(original_font,"face","Consolas");obs_data_set_int(original_font,"size",64);obs_data_set_obj(original,"font",original_font);
 auto *styled=obs_data_create();obs_data_set_obj(styled,"font",original_font);caption_apply_font(styled,40);
 check(obs_data_get_int(original_font,"size")==64,"manual font must preserve original OBS font object");
 auto *changed_font=obs_data_get_obj(styled,"font");check(obs_data_get_int(changed_font,"size")==40,"manual font applies to independent object");
 obs_data_release(changed_font);obs_data_release(styled);obs_data_release(original_font);obs_data_release(original);
 std::cout<<"real libobs browser protocol parsing, stale/session checks and config: PASS\n";
 if(argc>1 && std::string(argv[1])=="--serve"){
  auto token=browser_session_token();auto shared=std::make_shared<BrowserJournal>(token);
  std::string style=argc>2?argv[2]:"cathay";
  shared->configure("{\"style\":"+browser_json_string(style)+",\"background\":\"auto\",\"custom\":false}","");
  shared->publish(2,"The Celestial Dragon guards the jade gates. Let every word remain long enough to read before the next sentence arrives.",true);
  BrowserBridge bridge(std::filesystem::current_path()/"data"/"overlay",token,[shared]{return shared->state_json();},[shared,token](const std::string &body){return browser_acknowledge(*shared,token,body);},[]{return std::string{};});
  std::ofstream ready(".work/browser-host-url.txt");ready<<bridge.url();ready.close();std::cout<<"Browser host ready\n"<<std::flush;
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
  while(std::chrono::steady_clock::now()<deadline && !std::filesystem::exists(".work/browser-host-stop"))std::this_thread::sleep_for(std::chrono::milliseconds(100));
 }
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
