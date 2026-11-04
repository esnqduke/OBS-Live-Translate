#pragma once
#include "browser-bridge.hpp"
#include "browser-journal.hpp"
#include "caption-options.hpp"
#include <obs-module.h>
#include <util/bmem.h>
#include <memory>
#include <cstring>

inline bool browser_acknowledge(BrowserJournal &journal,const std::string &session,const std::string &body){
 auto *data=obs_data_create_from_json(body.c_str());if(!data)return false;
 bool valid=session==obs_data_get_string(data,"session");
 auto *array=obs_data_get_array(data,"ids");
 valid=valid && array && obs_data_array_count(array)<=BrowserJournal::max_phrases;
 std::vector<std::pair<uint64_t,uint64_t>> ids;
 if(valid){
  for(size_t index=0;index<obs_data_array_count(array);++index){
   auto *item=obs_data_array_item(array,index);std::string id=obs_data_get_string(item,"id");
   auto revision=obs_data_get_int(item,"revision");obs_data_release(item);
   if(id.empty() || id.size()>20 || revision<=0 || !std::all_of(id.begin(),id.end(),[](char c){return c>='0' && c<='9';})){valid=false;break;}
   try{ids.emplace_back(std::stoull(id),uint64_t(revision));}catch(...){valid=false;break;}
  }
 }
 if(array)obs_data_array_release(array);obs_data_release(data);
 if(valid)for(const auto &id:ids)journal.ack(id.first,id.second);
 return valid;
}
class BrowserOutput {
 std::string session=browser_session_token();
 std::shared_ptr<BrowserJournal> journal=std::make_shared<BrowserJournal>(session);
 std::unique_ptr<BrowserBridge> server;
public:
 BrowserOutput(const std::string &config,const std::string &image){
  char *path=obs_module_file("overlay");if(!path)throw std::runtime_error("Browser overlay assets missing");
  auto root=std::filesystem::u8path(path);bfree(path);
  if(!std::filesystem::is_regular_file(root/"index.html"))throw std::runtime_error("Browser overlay assets missing");
  journal->configure(config,image);auto shared=journal;auto identity=session;
  server=std::make_unique<BrowserBridge>(root,session,[shared]{return shared->state_json();},[shared,identity](const std::string &body){return browser_acknowledge(*shared,identity,body);},[shared]{return shared->image();});
 }
 bool publish(uint64_t id,const std::string &text,bool final){return journal->publish(id,text,final);}
 bool preview(const std::string &text){return journal->preview(text);}
 void configure(const std::string &config,const std::string &image){journal->configure(config,image);}
 std::string url() const{return server->url();}
 std::string error() const{return journal->error();}
 bool connected() const{return server->connected();}
 void clear(){journal->clear();}
 void deactivate(){journal->deactivate();}
 std::string state_json() const{return journal->state_json();}
};

inline std::string browser_style(obs_data_t *settings){
 std::string style=obs_data_get_string(settings,"caption_style");
 for(auto supported:{"plain","matrix","console","cyber","matrix_rain","fantasy","cathay"})if(style==supported)return style;
 return "plain";
}
inline std::string browser_color(int64_t number){
 const char hex[]="0123456789abcdef";std::string color="#";
 for(unsigned shift:{0u,8u,16u}){unsigned c=unsigned(uint64_t(number)>>shift)&255;color+=hex[c>>4];color+=hex[c&15];}return color;
}
struct CaptionNumber {const char *setting,*json,*label;int min,max,value,step;};
inline constexpr CaptionNumber caption_numbers[]={
 {"caption_reading_ms","readingMs","CaptionReadingMs",100,60000,3000,100},
 {"caption_character_ms","characterMs","CaptionCharacterMs",0,500,60,10},
 {"caption_line_interval_ms","lineIntervalMs","CaptionLineIntervalMs",0,30000,1500,100},
 {"caption_max_rows","maxRows","CaptionMaxRows",1,3,3,1},
 {"caption_animation_speed","animationSpeed","CaptionAnimationSpeed",10,500,100,10},
 {"caption_font_size","fontSize","CaptionFontSize",0,200,0,1}
};
inline bool caption_has(obs_data_t *s,const char *key){return obs_data_has_user_value(s,key) || obs_data_has_default_value(s,key);}
inline int caption_number(obs_data_t *s,const CaptionNumber &n){int v=int(std::clamp<int64_t>(caption_has(s,n.setting)?obs_data_get_int(s,n.setting):n.value,n.min,n.max));return std::strcmp(n.json,"fontSize")==0 && v?std::max(12,v):v;}
inline CaptionOptions caption_options(obs_data_t *s){
 CaptionOptions o;o.reading_ms=caption_number(s,caption_numbers[0]);o.character_ms=caption_number(s,caption_numbers[1]);o.line_interval_ms=caption_number(s,caption_numbers[2]);
 o.max_rows=caption_number(s,caption_numbers[3]);o.animation_speed=caption_number(s,caption_numbers[4]);o.font_size=caption_number(s,caption_numbers[5]);
 o.animations=!caption_has(s,"caption_animations") || obs_data_get_bool(s,"caption_animations");
 o.partial_results=!caption_has(s,"caption_partial_results") || obs_data_get_bool(s,"caption_partial_results");
 std::string a=obs_data_get_string(s,"caption_alignment");if(a=="left" || a=="center" || a=="right")o.alignment=a;
 return o;
}
inline void caption_apply_font(obs_data_t *settings,int size){
 auto *saved=obs_data_get_obj(settings,"font");auto *font=obs_data_create();
 if(saved){obs_data_apply(font,saved);obs_data_release(saved);}
 if(!*obs_data_get_string(font,"face"))obs_data_set_string(font,"face","Arial");
 obs_data_set_int(font,"size",size);obs_data_set_obj(settings,"font",font);obs_data_release(font);
}
inline void caption_defaults(obs_data_t *s,bool reset=false){
 for(const auto &n:caption_numbers){if(reset)obs_data_set_int(s,n.setting,n.value);else obs_data_set_default_int(s,n.setting,n.value);}
 for(auto key:{"caption_animations","caption_partial_results"}){if(reset)obs_data_set_bool(s,key,true);else obs_data_set_default_bool(s,key,true);}
 if(reset)obs_data_set_string(s,"caption_alignment","auto");else obs_data_set_default_string(s,"caption_alignment","auto");
}
inline std::string browser_config(obs_data_t *settings){
 auto *data=obs_data_create();obs_data_set_string(data,"style",browser_style(settings).c_str());
 const auto captions=caption_options(settings);
 for(const auto &n:caption_numbers)obs_data_set_int(data,n.json,caption_number(settings,n));
 obs_data_set_bool(data,"animations",captions.animations);obs_data_set_bool(data,"partialResults",captions.partial_results);obs_data_set_string(data,"alignment",captions.alignment.c_str());
 std::string background=obs_data_get_string(settings,"background_type");bool valid=false;
 for(auto type:{"auto","none","solid","gradient","parchment","cathay","image"})if(background==type)valid=true;
 obs_data_set_string(data,"background",valid?background.c_str():"auto");
 obs_data_set_bool(data,"custom",obs_data_get_bool(settings,"background_custom"));
 for(auto pair:{std::pair<const char *,const char *>{"background_color","color"},{"background_color2","color2"},{"background_border_color","borderColor"}})
  obs_data_set_string(data,pair.second,browser_color(obs_data_get_int(settings,pair.first)).c_str());
 for(auto item:{std::pair<const char *,int>{"opacity",100},{"paddingX",256},{"paddingY",128},{"radius",128},{"borderWidth",32}}){
  std::string key="background_"+std::string(item.first);obs_data_set_int(data,item.first,std::clamp<int64_t>(obs_data_get_int(settings,key.c_str()),0,item.second));
 }
 obs_data_set_string(data,"imageFit",std::strcmp(obs_data_get_string(settings,"background_image_fit"),"contain")==0?"contain":"cover");
 std::string path=obs_data_get_string(settings,"background_image");std::error_code error;
 auto modified=std::filesystem::last_write_time(std::filesystem::u8path(path),error);
 std::string image_version=std::to_string(std::hash<std::string>{}(path))+":"+(error?"0":std::to_string(modified.time_since_epoch().count()));
 obs_data_set_string(data,"imageVersion",image_version.c_str());
 std::string json=obs_data_get_json(data);obs_data_release(data);return json;
}
