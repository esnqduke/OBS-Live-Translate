#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
struct SupportedModel {uint64_t bytes;const char *sha256;const char *name;};
inline constexpr SupportedModel supported_models[]={
 {147951465,"60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe","ggml-base.bin"},
 {539212467,"19fea4b380c3a618ec4723c3eef2eb785ffba0d0538cf43f8f235e7b3b34220f","ggml-medium-q5_0.bin"},
 {3095033483ULL,"64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2","ggml-large-v3.bin"}
};
inline const SupportedModel *verified_model(uint64_t bytes,const std::string &digest){
 for(const auto &model:supported_models)if(model.bytes==bytes && digest==model.sha256)return &model;
 return nullptr;
}

inline bool rewind_model_file(FILE *file){
 if(!file)return false;
#ifdef _WIN32
 return ::_fseeki64(file,0,SEEK_SET)==0;
#else
 return ::fseeko(file,0,SEEK_SET)==0;
#endif
}
inline bool model_file_size(FILE *file,uint64_t &bytes){
 if(!file)return false;
#ifdef _WIN32
 if(::_fseeki64(file,0,SEEK_END)!=0)return false;
 const auto offset=::_ftelli64(file);
#else
 if(::fseeko(file,0,SEEK_END)!=0)return false;
 const auto offset=::ftello(file);
#endif
 if(offset<0 || !rewind_model_file(file))return false;
 bytes=static_cast<uint64_t>(offset);return true;
}
