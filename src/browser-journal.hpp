#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

inline std::string browser_json_string(const std::string &value){
 std::string result="\"";const char hex[]="0123456789abcdef";
 for(unsigned char c:value){
  if(c=='"' || c=='\\'){result+='\\';result+=char(c);}
  else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
  else result+=char(c);
 }
 return result+'"';
}
class BrowserJournal {
public:
 struct Phrase {uint64_t id=0,revision=1;std::string text;bool final=false;};
 static constexpr size_t max_bytes=256*1024,max_phrases=512;
private:
 mutable std::mutex mutex;
 std::map<uint64_t,Phrase> phrases;
 size_t bytes=0;
 uint64_t highest=0,preview_serial=0;
 bool active=true;
 std::string session,config="{}",failure,preview_text,background_path;
public:
 explicit BrowserJournal(std::string identity):session(std::move(identity)){}
 bool publish(uint64_t id,const std::string &text,bool final){
  std::lock_guard<std::mutex> lock(mutex);
  if(!active || !failure.empty())return false;
  auto it=phrases.find(id);
  if(it==phrases.end() && id<=highest)return true;
  if(it!=phrases.end() && it->second.final)return true;
  if(it==phrases.end() && text.empty()){highest=id;return true;}
  const std::string &value=it!=phrases.end() && text.empty() && final?it->second.text:text;
  size_t old=it==phrases.end()?0:it->second.text.size();
  if(value.size()>max_bytes-(bytes-old) || (it==phrases.end() && phrases.size()>=max_phrases)){
   failure="CaptionBacklogFull";return false;
  }
  if(it==phrases.end()){phrases.emplace(id,Phrase{id,1,value,final});highest=id;bytes+=value.size();}
  else if(it->second.text!=value || it->second.final!=final){bytes=bytes-old+value.size();it->second.text=value;it->second.final=final;++it->second.revision;}
  return true;
 }
 bool ack(uint64_t id,uint64_t revision){
  std::lock_guard<std::mutex> lock(mutex);auto it=phrases.find(id);
  if(it==phrases.end() || !it->second.final || it->second.revision!=revision)return false;
  bytes-=it->second.text.size();phrases.erase(it);return true;
 }
 std::vector<Phrase> snapshot() const{std::lock_guard<std::mutex> lock(mutex);std::vector<Phrase> result;for(const auto &item:phrases)result.push_back(item.second);return result;}
 std::string error() const{std::lock_guard<std::mutex> lock(mutex);return failure;}
 bool preview(const std::string &text){std::lock_guard<std::mutex> lock(mutex);if(!phrases.empty())return false;preview_text=text;++preview_serial;return true;}
 void configure(const std::string &json,const std::string &image){std::lock_guard<std::mutex> lock(mutex);config=json;background_path=image;}
 std::string image() const{std::lock_guard<std::mutex> lock(mutex);return background_path;}
 void deactivate(){std::lock_guard<std::mutex> lock(mutex);active=false;phrases.clear();bytes=0;preview_text.clear();}
 std::string state_json() const{
  std::lock_guard<std::mutex> lock(mutex);
  std::string result="{\"session\":"+browser_json_string(session)+",\"active\":"+(active?"true":"false")+",\"config\":"+config+",\"phrases\":[";
  bool first=true;for(const auto &item:phrases){const auto &p=item.second;if(!first)result+=',';first=false;
   result+="{\"id\":"+browser_json_string(std::to_string(p.id))+",\"revision\":"+std::to_string(p.revision)+",\"text\":"+browser_json_string(p.text)+",\"final\":"+(p.final?"true":"false")+"}";
  }
  result+="],\"preview\":{\"serial\":"+std::to_string(preview_serial)+",\"text\":"+browser_json_string(preview_text)+"},\"error\":"+browser_json_string(failure)+"}";
  return result;
 }
};
