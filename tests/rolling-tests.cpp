#include "../src/rolling-captions.hpp"
#include "../src/caption-layout.hpp"
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(){try{
 RollingCaptions roll;
 check(roll.update(1,{"one","two","three","four"},true,0),"accept long phrase");
 roll.advance(0);check(roll.text()=="one","first line enters immediately");
 roll.advance(1499);check(roll.text()=="one","pacing prevents immediate replacement");
 roll.advance(1500);check(roll.text()=="one\ntwo","new line enters below");
 roll.advance(3000);check(roll.text()=="one\ntwo\nthree","third line preserves older text");
 roll.advance(4500);check(roll.text()=="two\nthree\nfour","only oldest read line leaves");
 check(!roll.idle(7499),"last line gets three full readable seconds");
 check(roll.idle(7500),"idle only after all pending text was displayed and read");

 RollingCaptions slow(1,650);
 slow.update(1,{"first","second"},true,0);slow.advance(0);
 slow.mark_readable(5,650);
 slow.advance(3000);check(slow.text()=="first","entrance time excluded from reading time");
 slow.advance(3650);check(slow.text()=="second","narrow window waits for reading deadline");

 RollingCaptions revisions;
 revisions.update(7,{"hello"},false,0);revisions.advance(0);
 revisions.update(7,{"hello world","next"},true,1000);
 check(revisions.text()=="hello world","visible draft updated in place");
 revisions.advance(1500);check(revisions.text()=="hello world\nnext","final does not duplicate the partial");
 revisions.update(7,{"obsolete"},false,2000);
 check(revisions.text()=="hello world\nnext","late partial cannot overwrite final");
 revisions.update(8,{"hello world"},true,2000);revisions.advance(3000);
 check(revisions.text()=="hello world\nnext\nhello world","identical text from distinct phrases is retained");

 RollingCaptions stall;
 stall.update(1,{"a","b","c","d"},true,0);stall.advance(0);
 stall.advance(60000);check(stall.text()=="a\nb","late tick admits only one new line");
 stall.advance(60001);check(stall.text()=="a\nb","missed ticks never fast-forward unread lines");

 RollingCaptions rewrite;
 rewrite.update(1,{"old first","old last","pending"},false,0);rewrite.advance(0);rewrite.advance(1540);
 rewrite.update(1,{"corrected"},true,2000);
 check(rewrite.text()=="corrected","shorter correction removes obsolete draft rows without duplication");
 check(!rewrite.idle(4999) && rewrite.idle(5000),"changed visible line gets fresh reading time");

 RollingCaptions bounded;
 check(!bounded.update(1,{std::string(RollingCaptions::max_bytes+1,'x')},true,0),"oversized input explicitly rejected");
 check(bounded.text().empty(),"rejection does not publish truncated text");
 for(uint64_t id=1;id<=RollingCaptions::max_phrases;id++)check(bounded.update(id,{"x"},true,0),"bounded queue accepts up to limit");
 check(!bounded.update(1000,{"lost"},true,0),"phrase limit explicitly reported");
 bounded.clear();check(bounded.idle(0) && bounded.text().empty(),"stop resets pending and visible text");

 RollingCaptions unicode;
 std::string cyrillic;for(int i=0;i<50;i++)cyrillic+="\xD1\x8F";
 unicode.update(1,{cyrillic},true,0);unicode.advance(0);
 check(!unicode.idle(3999),"Unicode reading time counts characters, not UTF-8 bytes");
 check(unicode.idle(4000),"Unicode reading deadline deterministic");
 CaptionLayout layout("Arial",64,1,500,384);
 check(layout.rows()==3,"normal extent permits three rows");
 auto wrapped=layout.wrap("Hello   world this is a longer sentence with words.");
 check(wrapped.size()>1,"actual width splits a long sentence");
 for(const auto &line:wrapped)check(layout.fits(line),"wrapped rows fit measured font width including animation reserve");
 auto letters=layout.wrap(cyrillic);
 std::string joined;for(const auto &line:letters){check(layout.fits(line),"long Unicode word wraps within width");joined+=line;}
 check(joined==cyrillic,"long word wrapping neither drops bytes nor splits UTF-8");
 CaptionLayout narrow_layout("Consolas",64,1,500,80);
 check(narrow_layout.rows()==1,"small height reduces visible row count");
 RollingCaptions interrupted;
 interrupted.update(1,{"draft"},false,0);interrupted.advance(0);
 interrupted.update(2,{"new phrase"},true,1000);interrupted.advance(1500);
 check(interrupted.text()=="draft\nnew phrase","next phrase seals orphaned partial without blocking the queue");
 RollingCaptions after_retirement(1);
 after_retirement.update(1,{"already read","draft tail"},false,0);after_retirement.advance(0);after_retirement.advance(3000);
 after_retirement.update(1,{"revised beginning","final tail","remaining"},true,3500);
 check(after_retirement.text()=="final tail","revision updates visible tail without replaying retired beginning");
 after_retirement.advance(6499);check(after_retirement.text()=="final tail","corrected tail is protected for fresh reading time");
 after_retirement.advance(6500);check(after_retirement.text()=="remaining","remaining final text is eventually shown");
 RollingCaptions empty_final;
 empty_final.update(1,{"keep last draft"},false,0);empty_final.advance(0);
 empty_final.update(1,{},true,1000);empty_final.advance(1000);
 check(empty_final.text()=="keep last draft" && !empty_final.pending(),"empty final seals existing draft without erasing it");
 RollingCaptions stalled_animation(1,650);
 stalled_animation.update(1,{"unread","next"},true,0);stalled_animation.advance(0);
 stalled_animation.advance(60000);
 check(stalled_animation.text()=="unread" && !stalled_animation.idle(60000),"UI stall cannot retire an unrevealed row");
 stalled_animation.mark_readable(6,60000);
 stalled_animation.advance(62999);check(stalled_animation.text()=="unread","three seconds start at actual full frame");
 stalled_animation.advance(63000);check(stalled_animation.text()=="next","row advances after actual reading deadline");
 RollingCaptions resized;
 resized.update(1,{"first row","second row","third row","pending row"},true,0,"first row\nsecond row\nthird row\npending row");
 resized.advance(0);resized.advance(1540);resized.advance(3140);
 resized.reflow([](const std::string &text){std::vector<std::string> result;size_t start=0;for(size_t end;(end=text.find('\n',start))!=std::string::npos;start=end+1)result.push_back(text.substr(start,end-start));result.push_back(text.substr(start));return result;},1,4000);
 resized.advance(4000);check(resized.text()=="first row","resize requeues visible text at new one-row capacity");
 resized.advance(7000);check(resized.text()=="second row","resize keeps previous unread second row");
 resized.advance(10000);check(resized.text()=="third row","resize keeps previous unread third row");
 resized.advance(13000);check(resized.text()=="pending row","resize preserves pending final text");
 RollingCaptions resized_revision(1);
 resized_revision.update(1,{"gone","visible","pending"},false,0);resized_revision.advance(0);resized_revision.advance(3000);
 resized_revision.update(1,{"rewritten beginning","visible","pending"},false,3500);
 resized_revision.reflow([](const std::string &value){std::vector<std::string> result;size_t start=0;for(size_t end;(end=value.find('\n',start))!=std::string::npos;start=end+1)result.push_back(value.substr(start,end-start));result.push_back(value.substr(start));return result;},1,4000);
 resized_revision.advance(4000);
 check(resized_revision.text()=="visible","reflow after a rewrite does not resurrect retired beginning fragments");
 std::cout<<"rolling pacing, retention, revisions, stall and bounds: PASS\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
