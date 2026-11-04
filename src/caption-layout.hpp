#pragma once
// Matches OBS Text (GDI+): LOGFONT positive height and typographic measurement.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"gdi32.lib")

class CaptionLayout {
 ULONG_PTR token=0;
 HDC dc=nullptr;
 std::vector<HFONT> handles;
 std::vector<std::unique_ptr<Gdiplus::Font>> fonts;
 std::unique_ptr<Gdiplus::Graphics> graphics;
 float available=0;
 size_t count=1;
 static std::wstring wide(const std::string &text){
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
  if(!n && !text.empty())throw std::runtime_error("Invalid UTF-8 caption");
  std::wstring value(size_t(n),L' ');
  if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),value.data(),n);
  return value;
 }
 float width(const std::string &text) const{
  auto value=wide(text);float result=0;
  Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
  format.SetFormatFlags(Gdiplus::StringFormatFlagsNoFitBlackBox);
  for(const auto &font:fonts){
   Gdiplus::RectF bounds;
   if(graphics->MeasureString(value.c_str(),int(value.size()),font.get(),Gdiplus::PointF(0,0),&format,&bounds)!=Gdiplus::Ok)throw std::runtime_error("Caption measurement failed");
   result=std::max(result,bounds.Width);
  }
  return result;
 }
 void font(const std::string &name,int size,int flags){
  LOGFONTW lf{};lf.lfHeight=size;lf.lfWeight=flags&1?FW_BOLD:FW_DONTCARE;
  lf.lfItalic=bool(flags&2);lf.lfQuality=ANTIALIASED_QUALITY;lf.lfCharSet=DEFAULT_CHARSET;
  auto face=wide(name);wcsncpy_s(lf.lfFaceName,face.c_str(),_TRUNCATE);
  auto handle=CreateFontIndirectW(&lf);if(!handle)throw std::runtime_error("Caption font unavailable");
  handles.push_back(handle);fonts.push_back(std::make_unique<Gdiplus::Font>(dc,handle));
  if(fonts.back()->GetLastStatus()!=Gdiplus::Ok)throw std::runtime_error("Caption font unavailable");
 }
 void release(){fonts.clear();graphics.reset();for(auto handle:handles)DeleteObject(handle);handles.clear();if(dc)DeleteDC(dc);dc=nullptr;if(token)Gdiplus::GdiplusShutdown(token);token=0;}
public:
 CaptionLayout(const std::string &name,int size,int flags,int width_px,int height_px){
  Gdiplus::GdiplusStartupInput startup;
  if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok)throw std::runtime_error("GDI+ unavailable");
  try{
   dc=CreateCompatibleDC(nullptr);if(!dc)throw std::runtime_error("Caption DC unavailable");
   graphics=std::make_unique<Gdiplus::Graphics>(dc);
   font(name.empty()?"Arial":name,std::clamp(size,1,2000),flags);
   // All presets fit the same wrapped rows; switching style never reflows history.
   font("Consolas",64,1);font("Arial",64,1);
   float line_height=0;for(const auto &f:fonts)line_height=std::max(line_height,f->GetHeight(graphics.get()));
   if(height_px<line_height+8)throw std::runtime_error("Caption area too small");
   count=std::min<size_t>(3,size_t((height_px-8)/line_height));
   float widest=0;for(char c:std::string("01#$%&<>[]_"))widest=std::max(widest,width(std::string(1,c)));
   available=float(width_px)-8-5*widest;
   if(available<width("W"))throw std::runtime_error("Caption area too small");
  }catch(...){release();throw;}
 }
 ~CaptionLayout(){release();}
 CaptionLayout(const CaptionLayout &)=delete;
 CaptionLayout &operator=(const CaptionLayout &)=delete;
 size_t rows() const{return count;}
 bool fits(const std::string &text) const{return width(text)<=available;}
 std::vector<std::string> wrap(const std::string &text) const{
  // Collapse horizontal whitespace but preserve explicit line boundaries.
  std::string normalized;for(char c:text){if(c=='\r')continue;if(c=='\t')c=' ';if(c==' ' && (normalized.empty() || normalized.back()==' ' || normalized.back()=='\n'))continue;normalized+=c;}
  std::vector<std::string> lines;size_t start=0;
  while(start<normalized.size()){
   while(start<normalized.size() && (normalized[start]==' ' || normalized[start]=='\n'))++start;
   if(start==normalized.size())break;
   size_t limit=normalized.find('\n',start);if(limit==std::string::npos)limit=normalized.size();
   std::vector<size_t> ends;
   for(size_t i=start;i<limit;++i)if(i+1==limit || (static_cast<unsigned char>(normalized[i+1])&0xc0)!=0x80)ends.push_back(i+1);
   size_t lo=0,hi=ends.size();
   while(lo<hi){size_t mid=lo+(hi-lo)/2;if(fits(normalized.substr(start,ends[mid]-start)))lo=mid+1;else hi=mid;}
   if(!lo)throw std::runtime_error("Caption glyph exceeds available width");
   size_t end=ends[lo-1];
   if(end<limit){size_t space=normalized.rfind(' ',end);if(space!=std::string::npos && space>start)end=space;}
   size_t trimmed=end;while(trimmed>start && normalized[trimmed-1]==' ')--trimmed;
   lines.push_back(normalized.substr(start,trimmed-start));start=end;
  }
  return lines;
 }
};
