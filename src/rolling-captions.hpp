#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

// UI-thread model. All times are steady-clock milliseconds, never frame counts.
class RollingCaptions {
 struct Phrase {uint64_t id;std::vector<std::string> lines;size_t next=0,bytes=0;bool final=false;std::string source;size_t retired=0,base=0;};
 struct Row {uint64_t id;size_t index;std::string text;int64_t read_until;bool readable;};
 std::deque<Phrase> phrases;
 std::deque<Row> rows;
 size_t capacity,bytes=0;
 int64_t entrance_ms,next_line=0;
 uint64_t completed=0;
 bool have_completed=false;
 std::function<std::vector<std::string>(const std::string &)> rewrapper;
 static size_t characters(const std::string &text){size_t n=0;for(unsigned char c:text)if((c&0xc0)!=0x80)++n;return n;}
 static int64_t interval(const std::string &text){return std::max<int64_t>(1500,1000+60*int64_t(characters(text)));}
 int64_t deadline(const std::string &text,int64_t now) const{return now+entrance_ms+std::max<int64_t>(3000,interval(text));}
 static bool space(char c){return c==' ' || c=='\n' || c=='\r' || c=='\t';}
 static size_t content_characters(const std::string &text){size_t n=0;for(unsigned char c:text)if(!space(char(c)) && (c&0xc0)!=0x80)++n;return n;}
 static std::string skip(const std::string &text,size_t count){
  size_t pos=0;while(pos<text.size() && count){if(!space(text[pos]) && (static_cast<unsigned char>(text[pos])&0xc0)!=0x80)--count;++pos;}
  while(pos<text.size() && ((static_cast<unsigned char>(text[pos])&0xc0)==0x80 || space(text[pos])))++pos;
  return text.substr(pos);
 }
 static std::string join(const std::vector<std::string> &lines){std::string result;for(const auto &line:lines){if(!result.empty())result+='\n';result+=line;}return result;}
 void prune(){
  while(!phrases.empty() && phrases.front().final && phrases.front().next>=phrases.front().lines.size()){
   completed=std::max(completed,phrases.front().id);have_completed=true;bytes-=phrases.front().bytes;phrases.pop_front();
  }
 }
public:
 static constexpr size_t max_bytes=256*1024,max_phrases=512;
 explicit RollingCaptions(size_t lines=3,int64_t animation_ms=0):capacity(std::clamp<size_t>(lines,1,3)),entrance_ms(animation_ms){}
 bool update(uint64_t id,const std::vector<std::string> &input_lines,bool final,int64_t now,const std::string &input_source={}){
  auto it=std::find_if(phrases.begin(),phrases.end(),[&](const Phrase &p){return p.id==id;});
  if(it==phrases.end() && have_completed && id<=completed)return true;
  if(it!=phrases.end() && it->final)return true;
  if(it!=phrases.end() && input_lines.empty() && final){it->final=true;return true;}
  std::string source=input_source.empty()?join(input_lines):input_source;
  auto lines=it!=phrases.end() && it->base && rewrapper?rewrapper(skip(source,it->base)):input_lines;
  size_t count=source.size();if(count>max_bytes)return false;
  for(const auto &line:lines){if(line.size()>max_bytes-count)return false;count+=line.size();}
  size_t old=it==phrases.end()?0:it->bytes;
  if(count>max_bytes-(bytes-old) || (it==phrases.end() && phrases.size()>=max_phrases))return false;
  if(it==phrases.end()){
   // Worker publishes phrase IDs in order; reject stale partials from a prior phrase.
   if(!phrases.empty() && id<phrases.back().id)return true;
   if(!phrases.empty())phrases.back().final=true;
   phrases.push_back({id,lines,0,count,final,source});bytes+=count;
  }else{
   // A blank final is a completion marker, not a request to erase its last draft.
   bytes=bytes-old+count;it->bytes=count;it->lines=lines;it->final=final;it->source=source;
   it->next=std::min(it->next,lines.size());
   for(auto row=rows.begin();row!=rows.end();){
    if(row->id!=id){++row;continue;}
    if(row->index>=lines.size()){row=rows.erase(row);continue;}
    if(row->text!=lines[row->index]){row->text=lines[row->index];row->read_until=deadline(row->text,now);row->readable=entrance_ms==0;}
    ++row;
   }
   size_t first=it->next;for(const auto &row:rows)if(row.id==id)first=std::min(first,row.index);
   it->retired=it->base;for(size_t i=0;i<first;++i)it->retired+=content_characters(lines[i]);
  }
  return true;
 }
 bool advance(int64_t now){
  prune();
  if(phrases.empty() || now<next_line)return false;
  auto &phrase=phrases.front();
  if(phrase.next>=phrase.lines.size())return false;
  if(rows.size()>=capacity){
   if(!rows.front().readable || now<rows.front().read_until)return false;
   for(auto &p:phrases)if(p.id==rows.front().id)p.retired+=content_characters(rows.front().text);
   rows.pop_front();
  }
  const auto &line=phrase.lines[phrase.next];
  rows.push_back({phrase.id,phrase.next++,line,deadline(line,now),entrance_ms==0});
  // Never catch up missed frames: every admitted row gets real screen time.
  next_line=now+interval(line);prune();return true;
 }
 std::string text() const{std::string result;for(const auto &row:rows){if(!result.empty())result+='\n';result+=row.text;}return result;}
 bool pending() const{for(const auto &phrase:phrases)if(phrase.next<phrase.lines.size() || !phrase.final)return true;return false;}
 bool idle(int64_t now) const{if(pending())return false;for(const auto &row:rows)if(!row.readable || now<row.read_until)return false;return true;}
 void mark_readable(size_t revealed_bytes,int64_t now){
  size_t end=0;for(auto &row:rows){end+=row.text.size();if(end<=revealed_bytes && !row.readable){row.readable=true;row.read_until=now+std::max<int64_t>(3000,interval(row.text));}++end;}
 }
 bool reflow(std::function<std::vector<std::string>(const std::string &)> wrap,size_t line_capacity,int64_t now){
  // Requeue only still-visible and pending text. Retired text stays retired.
  std::map<uint64_t,Phrase> retained;
  for(const auto &p:phrases)retained.emplace(p.id,p);
  for(const auto &row:rows){
   if(std::any_of(phrases.begin(),phrases.end(),[&](const Phrase &p){return p.id==row.id;}))continue;
   auto &p=retained[row.id];p.id=row.id;p.final=true;
   if(!p.source.empty())p.source+='\n';p.source+=row.text;
  }
  std::deque<Phrase> replacement;size_t total=0;
  for(auto &entry:retained){
   auto &p=entry.second;p.lines=wrap(skip(p.source,p.retired));p.next=0;p.base=p.retired;p.bytes=p.source.size();
   for(const auto &line:p.lines)p.bytes+=line.size();
   if(p.bytes>max_bytes-total)return false;
   total+=p.bytes;replacement.push_back(std::move(p));
  }
  phrases=std::move(replacement);rows.clear();bytes=total;capacity=std::clamp<size_t>(line_capacity,1,3);next_line=now;rewrapper=std::move(wrap);return true;
 }
 bool empty() const{return rows.empty() && phrases.empty();}
 void clear(){phrases.clear();rows.clear();bytes=0;next_line=0;completed=0;have_completed=false;rewrapper={};}
};
