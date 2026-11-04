#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

struct CaptionOptions {
 int64_t reading_ms=3000,character_ms=60,line_interval_ms=1500;
 int max_rows=3,animation_speed=100,font_size=0;
 bool animations=true,partial_results=true;
 std::string alignment="auto";
 static size_t characters(const std::string &text){size_t n=0;for(unsigned char c:text)if((c&0xc0)!=0x80)++n;return n;}
 int64_t reading(const std::string &text) const{return character_ms?std::max(reading_ms,1000+character_ms*int64_t(characters(text))):reading_ms;}
 int64_t interval(const std::string &text) const{return character_ms?std::max(line_interval_ms,1000+character_ms*int64_t(characters(text))):line_interval_ms;}
 int64_t effect_ms(const std::string &style) const{return !animations || style=="plain"?0:650*100/animation_speed;}
};
