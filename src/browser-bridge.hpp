#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"bcrypt.lib")

inline std::string browser_session_token(){
 unsigned char bytes[16];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Browser token generation failed");
 const char hex[]="0123456789abcdef";std::string token;
 for(auto b:bytes){token+=hex[b>>4];token+=hex[b&15];}return token;
}

// No OBS source access on this thread. Callbacks own an independent journal.
class BrowserBridge {
 using Clock=std::chrono::steady_clock;
 SOCKET listener=INVALID_SOCKET;
 std::atomic<bool> closing{false};
 std::atomic<int64_t> polled{0};
 std::thread thread;
 std::filesystem::path root;
 std::string token,origin,address;
 bool winsock=false;
 std::function<std::string()> state;
 std::function<bool(const std::string &)> acknowledge;
 std::function<std::string()> background;
 static int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
 static std::string lower(std::string value){for(char &c:value)c=char(std::tolower(static_cast<unsigned char>(c)));return value;}
 static std::string trim(std::string value){auto start=value.find_first_not_of(" \t");if(start==std::string::npos)return {};return value.substr(start,value.find_last_not_of(" \t")-start+1);}
 static void respond(SOCKET client,int code,const std::string &type,const std::string &body){
  std::string response="HTTP/1.1 "+std::to_string(code)+(code==200?" OK":" Error")+"\r\nContent-Type: "+type+"\r\nContent-Length: "+std::to_string(body.size())+
   "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; font-src 'self'; base-uri 'none'; frame-ancestors 'none'\r\n\r\n"+body;
  size_t sent=0;auto deadline=Clock::now()+std::chrono::seconds(2);
  while(sent<response.size() && Clock::now()<deadline){int n=send(client,response.data()+sent,int(std::min<size_t>(response.size()-sent,65536)),0);if(n<=0)break;sent+=size_t(n);}
 }
 static bool file(const std::filesystem::path &path,size_t limit,std::string &body){
  std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)return false;
  auto size=input.tellg();if(size<0 || uint64_t(size)>limit)return false;
  body.resize(size_t(size));input.seekg(0);if(!body.empty())input.read(body.data(),std::streamsize(body.size()));return bool(input);
 }
 void request(SOCKET client){
  constexpr size_t max_header=8192,max_body=65536;
  DWORD timeout=300;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char *>(&timeout),sizeof(timeout));
  setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char *>(&timeout),sizeof(timeout));
  std::string bytes;char buffer[4096];auto deadline=Clock::now()+std::chrono::seconds(2);size_t header_end=std::string::npos;
  while(!closing && Clock::now()<deadline){
   int n=recv(client,buffer,sizeof(buffer),0);if(n<=0)return;bytes.append(buffer,size_t(n));
   header_end=bytes.find("\r\n\r\n");if(header_end!=std::string::npos)break;
   if(bytes.size()>max_header){respond(client,413,"text/plain","Request too large");return;}
  }
  if(header_end==std::string::npos || header_end>max_header)return;
  std::istringstream headers(bytes.substr(0,header_end));std::string method,path,version,line;
  std::getline(headers,line);std::istringstream first(line);std::string extra;
  if(!(first>>method>>path>>version) || (first>>extra) || (version!="HTTP/1.1" && version!="HTTP/1.0")){respond(client,400,"text/plain","Bad request");return;}
  std::map<std::string,std::string> values;
  while(std::getline(headers,line)){
   if(!line.empty() && line.back()=='\r')line.pop_back();auto colon=line.find(':');
   if(colon==std::string::npos){respond(client,400,"text/plain","Bad header");return;}
   auto key=lower(trim(line.substr(0,colon)));
   if(key.empty() || values.count(key)){respond(client,400,"text/plain","Duplicate header");return;}
   values.emplace(key,trim(line.substr(colon+1)));
  }
  if(values["host"]!=address || (values.count("origin") && values["origin"]!=origin)){
   respond(client,403,"text/plain","Forbidden");return;
  }
  if(values.count("transfer-encoding")){respond(client,400,"text/plain","Unsupported transfer encoding");return;}
  size_t length=0;
  if(values.count("content-length")){
   auto value=values["content-length"];
   if(value.empty() || value.size()>6 || !std::all_of(value.begin(),value.end(),[](char c){return c>='0' && c<='9';})){respond(client,400,"text/plain","Bad length");return;}
   length=size_t(std::stoul(value));if(length>max_body){respond(client,413,"text/plain","Request too large");return;}
  }
  while(bytes.size()<header_end+4+length && !closing && Clock::now()<deadline){int n=recv(client,buffer,sizeof(buffer),0);if(n<=0)return;bytes.append(buffer,size_t(n));}
  if(bytes.size()<header_end+4+length)return;
  auto query=path.find('?');if(query!=std::string::npos)path.resize(query);
  const auto prefix="/"+token+"/";
  if(path.compare(0,prefix.size(),prefix)!=0){respond(client,404,"text/plain","Not found");return;}
  path.erase(0,prefix.size());
  if(method=="GET" && path=="state"){polled=now();respond(client,200,"application/json; charset=utf-8",state());return;}
  if(method=="POST" && path=="ack"){
   if(!values.count("content-type") || values["content-type"].find("application/json")!=0){respond(client,415,"text/plain","JSON required");return;}
   bool accepted=acknowledge(bytes.substr(header_end+4,length));respond(client,accepted?200:400,"application/json",accepted?"{}":"{\"error\":\"Invalid acknowledgement\"}");return;
  }
  if(method!="GET"){respond(client,405,"text/plain","Method not allowed");return;}
  std::string body,type;
  if(path=="background"){
   auto image=std::filesystem::u8path(background());auto ext=lower(image.extension().u8string());
   if(ext==".png")type="image/png";else if(ext==".jpg" || ext==".jpeg")type="image/jpeg";else if(ext==".webp")type="image/webp";
   if(type.empty() || !file(image,16*1024*1024,body)){respond(client,404,"text/plain","Image unavailable");return;}
  }else{
   if(path=="index.html")type="text/html; charset=utf-8";
   else if(path=="overlay.css")type="text/css; charset=utf-8";
   else if(path=="overlay.js" || path=="model.mjs")type="application/javascript; charset=utf-8";
   else{respond(client,404,"text/plain","Not found");return;}
   if(!file(root/path,2*1024*1024,body)){respond(client,404,"text/plain","Asset unavailable");return;}
  }
  respond(client,200,type,body);
 }
 void serve(){
  while(!closing){
   fd_set set;FD_ZERO(&set);FD_SET(listener,&set);timeval delay{0,100000};
   int ready=select(0,&set,nullptr,nullptr,&delay);if(ready<=0)continue;
   auto client=accept(listener,nullptr,nullptr);if(client==INVALID_SOCKET)continue;
   try{request(client);}catch(...){respond(client,500,"text/plain","Request failed");}
   closesocket(client);
  }
 }
public:
 BrowserBridge(std::filesystem::path assets,std::string identity,std::function<std::string()> snapshot,std::function<bool(const std::string &)> ack,std::function<std::string()> image):root(std::move(assets)),token(std::move(identity)),state(std::move(snapshot)),acknowledge(std::move(ack)),background(std::move(image)){
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Browser Winsock initialization failed");winsock=true;
  try{
   listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==INVALID_SOCKET)throw std::runtime_error("Browser socket unavailable");
   BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char *>(&exclusive),sizeof(exclusive));
   sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
   if(bind(listener,reinterpret_cast<sockaddr *>(&local),sizeof(local)) || listen(listener,4))throw std::runtime_error("Browser loopback bind failed");
   int size=sizeof(local);if(getsockname(listener,reinterpret_cast<sockaddr *>(&local),&size))throw std::runtime_error("Browser socket address unavailable");
   address="127.0.0.1:"+std::to_string(ntohs(local.sin_port));origin="http://"+address;
   thread=std::thread([this]{serve();});
  }catch(...){if(listener!=INVALID_SOCKET)closesocket(listener);WSACleanup();winsock=false;throw;}
 }
 ~BrowserBridge(){closing=true;if(thread.joinable())thread.join();if(listener!=INVALID_SOCKET)closesocket(listener);if(winsock)WSACleanup();}
 BrowserBridge(const BrowserBridge &)=delete;
 BrowserBridge &operator=(const BrowserBridge &)=delete;
 std::string url() const{return origin+"/"+token+"/index.html";}
 bool connected() const{return polled.load()!=0 && now()-polled.load()<2500;}
};
