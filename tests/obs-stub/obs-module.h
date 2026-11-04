#pragma once
// Focused test double for filter lifecycle/UI dispatch, not an OBS runtime.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include "../../src/browser-journal.hpp"
#define MAX_AV_PLANES 8
enum {LOG_WARNING,LOG_ERROR,LOG_INFO,OBS_TASK_UI,OBS_SOURCE_TYPE_FILTER,OBS_SOURCE_AUDIO,
 OBS_PATH_FILE,OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING,OBS_TEXT_INFO};
struct obs_source_t {std::string id="text_gdiplus",name="captions";bool enabled=true,removed=false,muted=false;obs_source_t *parent=nullptr;};
struct obs_data_t {std::map<std::string,std::string> strings;std::map<std::string,bool> bools;int64_t threads=2;std::map<std::string,int64_t> ints;std::map<std::string,std::shared_ptr<obs_data_t>> objects;std::string json;};
struct obs_data_array_t {std::vector<std::shared_ptr<obs_data_t>> items;};
struct obs_property_t {std::string description;std::vector<std::string> items;};
struct obs_properties_t {std::map<std::string,obs_property_t> entries;};
struct calldata_t {bool enabled;};
struct obs_audio_data {uint8_t *data[8]{};uint32_t frames=0;};
struct obs_audio_info {int speakers=2;uint32_t samples_per_sec=48000;};
struct obs_source_info {
 const char *id=nullptr;int type=0,output_flags=0;
 const char *(*get_name)(void *)=nullptr;
 void *(*create)(obs_data_t *,obs_source_t *)=nullptr;
 void (*destroy)(void *)=nullptr;
 void (*update)(void *,obs_data_t *)=nullptr;
 obs_audio_data *(*filter_audio)(void *,obs_audio_data *)=nullptr;
 void (*video_tick)(void *,float)=nullptr;
 void (*filter_add)(void *,obs_source_t *)=nullptr;
 void (*filter_remove)(void *,obs_source_t *)=nullptr;
 obs_properties_t *(*get_properties)(void *)=nullptr;
 void (*get_defaults)(obs_data_t *)=nullptr;
};
inline obs_source_info registered;
inline obs_source_t output;
inline bool output_exists=true;
inline int updates=0,references=0;
inline std::string written;
inline obs_data_t appearance;
inline bool written_from_file=false;
inline std::vector<std::pair<void (*)(void *),void *>> tasks;
inline bool inline_ui_tasks=false;
inline void obs_register_source(obs_source_info *p){registered=*p;}
inline const char *obs_module_text(const char *s){return s;}
inline void blog(int,const char *,...){}
inline const char *obs_source_get_unversioned_id(obs_source_t *s){return s->id.c_str();}
inline bool obs_source_removed(obs_source_t *s){return s->removed;}
inline const char *obs_source_get_name(obs_source_t *s){return s->name.c_str();}
inline const char *obs_source_get_uuid(obs_source_t *){return "test-filter-owner";}
inline char *obs_module_file(const char *name){auto path=(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"data"/name).u8string();return _strdup(path.c_str());}
inline bool obs_source_enabled(obs_source_t *s){return s->enabled;}
inline bool obs_source_muted(obs_source_t *s){return s->muted;}
inline obs_source_t *obs_filter_get_parent(obs_source_t *s){return s->parent;}
inline obs_source_t *obs_get_source_by_name(const char *s){if(output_exists && output.name==s){++references;return &output;}return nullptr;}
inline void obs_source_release(obs_source_t *){--references;}
inline obs_data_t *obs_data_create(){return new obs_data_t;}
inline void obs_data_apply(obs_data_t *dst,obs_data_t *src){*dst=*src;}
inline void obs_data_release(obs_data_t *s){delete s;}
inline void obs_data_set_bool(obs_data_t *s,const char *k,bool v){s->bools[k]=v;}
inline bool obs_data_get_bool(obs_data_t *s,const char *k){auto it=s->bools.find(k);return it!=s->bools.end() && it->second;}
inline void obs_data_set_default_bool(obs_data_t *s,const char *k,bool v){s->bools.emplace(k,v);}
inline void obs_data_set_string(obs_data_t *s,const char *k,const char *v){s->strings[k]=v;}
inline const char *obs_data_get_string(obs_data_t *s,const char *k){return s->strings[k].c_str();}
inline void obs_data_set_default_string(obs_data_t *s,const char *k,const char *v){s->strings.emplace(k,v);}
inline bool obs_data_has_user_value(obs_data_t *s,const char *k){return s->strings.count(k)||s->ints.count(k)||s->bools.count(k);}
inline bool obs_data_has_default_value(obs_data_t *,const char *){return false;}
inline int64_t obs_data_get_int(obs_data_t *s,const char *k){return std::string(k)=="cpu_threads"?s->threads:s->ints[k];}
inline void obs_data_set_int(obs_data_t *s,const char *k,int64_t v){if(std::string(k)=="cpu_threads")s->threads=v;else s->ints[k]=v;}
inline obs_data_t *obs_data_get_obj(obs_data_t *s,const char *k){auto it=s->objects.find(k);return it==s->objects.end()?nullptr:new obs_data_t(*it->second);}
inline void obs_data_set_obj(obs_data_t *s,const char *k,obs_data_t *v){s->objects[k]=std::make_shared<obs_data_t>(*v);}
inline void obs_data_set_default_int(obs_data_t *s,const char *key,int64_t v){if(std::string(key)=="cpu_threads")s->threads=v;else s->ints.emplace(key,v);}
// Parser behavior is exercised against real libobs in the browser host harness.
inline obs_data_t *obs_data_create_from_json(const char *){return nullptr;}
inline obs_data_array_t *obs_data_get_array(obs_data_t *,const char *){return nullptr;}
inline size_t obs_data_array_count(obs_data_array_t *a){return a->items.size();}
inline obs_data_t *obs_data_array_item(obs_data_array_t *a,size_t i){return new obs_data_t(*a->items.at(i));}
inline void obs_data_array_release(obs_data_array_t *a){delete a;}
inline const char *obs_data_get_json(obs_data_t *s){s->json="{";bool first=true;auto add=[&](const std::string &k,const std::string &v){if(!first)s->json+=',';first=false;s->json+=browser_json_string(k)+":"+v;};for(const auto &p:s->strings)add(p.first,browser_json_string(p.second));for(const auto &p:s->ints)add(p.first,std::to_string(p.second));for(const auto &p:s->bools)add(p.first,p.second?"true":"false");s->json+='}';return s->json.c_str();}
inline obs_data_t *obs_source_get_settings(obs_source_t *){auto *s=new obs_data_t(appearance);s->strings["text"]=written;s->bools["read_from_file"]=written_from_file;return s;}
inline void obs_source_update(obs_source_t *,obs_data_t *s){++updates;for(auto &v:s->strings)appearance.strings[v.first]=v.second;for(auto &v:s->ints)appearance.ints[v.first]=v.second;for(auto &v:s->bools)appearance.bools[v.first]=v.second;for(auto &v:s->objects)appearance.objects[v.first]=v.second;if(s->strings.count("text"))written=s->strings["text"];if(s->bools.count("read_from_file"))written_from_file=s->bools["read_from_file"];}
inline void obs_queue_task(int,void (*fn)(void *),void *p,bool){if(inline_ui_tasks)fn(p);else tasks.emplace_back(fn,p);}
inline void drain(){auto pending=std::move(tasks);tasks.clear();for(auto &t:pending)t.first(t.second);}
inline void *obs_source_get_signal_handler(obs_source_t *){return nullptr;}
inline void signal_handler_connect(void *,const char *,void (*)(void *,calldata_t *),void *){}
inline void signal_handler_disconnect(void *,const char *,void (*)(void *,calldata_t *),void *){}
inline bool calldata_bool(calldata_t *d,const char *){return d->enabled;}
inline bool obs_get_audio_info(obs_audio_info *p){*p={};return true;}
inline size_t get_audio_channels(int s){return size_t(s);}
inline obs_properties_t *obs_properties_create(){return new obs_properties_t;}
inline obs_property_t *obs_properties_get(obs_properties_t *p,const char *s){return &p->entries[s];}
inline obs_property_t *obs_properties_add_path(obs_properties_t *p,const char *s,const char *,int,const char *,const char *){return obs_properties_get(p,s);}
inline obs_property_t *obs_properties_add_list(obs_properties_t *p,const char *s,const char *,int,int){return obs_properties_get(p,s);}
inline obs_property_t *obs_properties_add_int(obs_properties_t *p,const char *s,const char *,int,int,int){return obs_properties_get(p,s);}
inline obs_property_t *obs_properties_add_color(obs_properties_t *p,const char *s,const char *){return obs_properties_get(p,s);}
inline obs_property_t *obs_properties_add_bool(obs_properties_t *p,const char *s,const char *){return obs_properties_get(p,s);}
inline obs_property_t *obs_properties_add_text(obs_properties_t *p,const char *s,const char *,int){return obs_properties_get(p,s);}
inline obs_property_t *obs_properties_add_button(obs_properties_t *p,const char *s,const char *,bool (*)(obs_properties_t *,obs_property_t *,void *)){return obs_properties_get(p,s);}
inline void obs_property_set_description(obs_property_t *p,const char *s){p->description=s;}
inline void obs_property_list_add_string(obs_property_t *p,const char *s,const char *){p->items.emplace_back(s);}
inline void obs_property_list_clear(obs_property_t *p){p->items.clear();}
inline void obs_enum_sources(bool (*fn)(void *,obs_source_t *),void *p){if(output_exists)fn(p,&output);}
