#include "../src/browser-journal.hpp"
#include "../src/browser-bridge.hpp"
#include <iostream>
#include <stdexcept>
void check(bool b,const char *message){if(!b)throw std::runtime_error(message);}
std::string http(const std::string &url,const std::string &route,const std::string &method="GET",const std::string &body="",const std::string &host_override="",const std::string &origin=""){
 auto slash=url.find('/',7);auto host=url.substr(7,slash-7);auto colon=host.find(':');auto token_end=url.find_last_of('/');
 std::string path=url.substr(slash,token_end-slash+1)+route;
 SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(client!=INVALID_SOCKET,"client socket");
 sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);local.sin_port=htons(static_cast<u_short>(std::stoi(host.substr(colon+1))));
 check(connect(client,reinterpret_cast<sockaddr *>(&local),sizeof(local))==0,"connect loopback");
 DWORD timeout=2000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char *>(&timeout),sizeof(timeout));
 std::string request=method+" "+path+" HTTP/1.1\r\nHost: "+(host_override.empty()?host:host_override)+"\r\n";
 if(!origin.empty())request+="Origin: "+origin+"\r\n";
 request+="Content-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
 check(send(client,request.data(),int(request.size()),0)==int(request.size()),"send request");
 std::string response;char buffer[4096];for(int n;(n=recv(client,buffer,sizeof(buffer),0))>0;)response.append(buffer,size_t(n));closesocket(client);return response;
}
int main(){try{
 BrowserJournal journal("session");
 check(journal.publish(2,"draft",false),"partial accepted");
 check(journal.publish(2,"final text",true),"same phrase finalized");
 check(journal.publish(3,"next final",true),"next final retained");
 auto items=journal.snapshot();
 check(items.size()==2 && items[0].text=="final text" && items[0].revision==2,"revisions replace draft without losing next final");
 check(!journal.ack(2,1),"stale acknowledgement cannot erase revised final");
 check(journal.ack(2,2),"exact final ack accepted");
 journal.publish(2,"late partial",false);check(journal.snapshot().size()==1,"late duplicate cannot resurrect read phrase");
 journal.publish(4,"abandoned draft",false);journal.publish(4,"",true);
 items=journal.snapshot();check(items.back().text=="abandoned draft" && items.back().final,"empty final closes existing draft");
 check(!journal.ack(4,1),"pre-final ack rejected");
 BrowserJournal bounded("bounded");
 check(!bounded.publish(1,std::string(BrowserJournal::max_bytes+1,'x'),true),"oversize explicitly rejected");
 check(bounded.snapshot().empty() && !bounded.error().empty(),"overflow is visible and never publishes truncation");
 BrowserJournal load("load");for(uint64_t id=1;id<=BrowserJournal::max_phrases;id++)check(load.publish(id,"x",true),"bounded queue accepts allowed count");
 check(!load.publish(1000,"overflow",true),"phrase bound enforced");
 check(!load.preview("sample"),"preview cannot interrupt pending speech");
 BrowserJournal preview("preview");check(preview.preview("sample"),"idle preview accepted");
 check(preview.state_json().find("sample")!=std::string::npos,"preview serialized");
 preview.deactivate();check(preview.snapshot().empty() && preview.state_json().find("\"active\":false")!=std::string::npos,"deactivation hides page and clears session");
 check(!preview.publish(1,"late speech",true),"stopped session refuses late worker delivery");
 BrowserJournal escaped("escaped");escaped.publish(1,"\"\\\n<script> & кириллица",true);
 auto json=escaped.state_json();check(json.find("\\\"\\\\")!=std::string::npos && json.find('\n')==std::string::npos,"JSON text escapes quotes/backslashes/control characters");
 auto assets=std::filesystem::current_path()/".work"/"browser-http-assets";
 std::filesystem::create_directories(assets);{std::ofstream file(assets/"index.html");file<<"<html>local overlay</html>";}
 std::string received;
 BrowserBridge bridge(assets,browser_session_token(),[&]{return journal.state_json();},[&](const std::string &body){received=body;return body=="{}";},[]{return std::string{};});
 auto response=http(bridge.url(),"index.html");check(response.find("200 OK")!=std::string::npos && response.find("local overlay")!=std::string::npos,"fixed local asset served");
 check(response.find("Cache-Control: no-store")!=std::string::npos && response.find("Content-Security-Policy:")!=std::string::npos,"page has cache and content restrictions");
 check(http(bridge.url(),"state").find("final text")==std::string::npos && bridge.connected(),"state snapshot reflects ack and records heartbeat");
 check(http(bridge.url(),"ack","POST","{}").find("200 OK")!=std::string::npos && received=="{}","ack body reaches handler");
 check(http(bridge.url(),"ack","POST","bad").find("400 Error")!=std::string::npos,"invalid acknowledgement rejected");
 check(http(bridge.url(),"../index.html").find("404 Error")!=std::string::npos,"arbitrary paths rejected");
 check(http(bridge.url(),"state","GET","","attacker.example").find("403 Error")!=std::string::npos,"foreign Host rejected");
 check(http(bridge.url(),"state","GET","","","https://attacker.example").find("403 Error")!=std::string::npos,"foreign Origin rejected");
 check(http(bridge.url(),"background").find("404 Error")!=std::string::npos,"unselected file is never served");
 auto wrong_url=bridge.url();wrong_url.insert(wrong_url.find_last_of('/'),"wrong-token");
 check(http(wrong_url,"state").find("404 Error")!=std::string::npos,"wrong token rejected");
 check(http(bridge.url(),"state","DELETE").find("405 Error")!=std::string::npos,"unsupported method rejected");
 check(http(bridge.url(),"ack","POST",std::string(65537,'x')).find("413 Error")!=std::string::npos,"oversized request rejected before parsing");
 std::cout<<"browser journal revisions, exact acknowledgements, bounds and preview: PASS\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
