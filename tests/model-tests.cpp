#include "transcription-worker.hpp"
#include "supported-models.hpp"
#include <condition_variable>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif
int main(){
 if(!verified_model(3095033483ULL,"64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2")){std::cerr<<"Pinned full large-v3 is not supported\n";return 1;}
 if(!verified_model(147951465,"60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe") ||
    !verified_model(539212467,"19fea4b380c3a618ec4723c3eef2eb785ffba0d0538cf43f8f235e7b3b34220f") ||
    verified_model(539212467,"bad") || verified_model(1,"19fea4b380c3a618ec4723c3eef2eb785ffba0d0538cf43f8f235e7b3b34220f"))return 1;
#ifdef _WIN32
 _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
 _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
#ifdef _WIN32
 // Sparse logical length exercises the real Windows CRT boundary without
 // allocating three gigabytes of disk space. First byte checks rewind too.
 auto large_path=std::filesystem::current_path()/"obs-stt-large-size-probe.bin";
 HANDLE handle=CreateFileW(large_path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(handle==INVALID_HANDLE_VALUE)return 1;
 DWORD ignored=0;LARGE_INTEGER end{};end.QuadPart=3095033483LL;
 bool created=DeviceIoControl(handle,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&ignored,nullptr) &&
  SetFilePointerEx(handle,end,nullptr,FILE_BEGIN) && SetEndOfFile(handle);
 end.QuadPart=0;const char marker=42;
 created=created && SetFilePointerEx(handle,end,nullptr,FILE_BEGIN) && WriteFile(handle,&marker,1,&ignored,nullptr);
 CloseHandle(handle);if(!created){std::filesystem::remove(large_path);return 1;}
 FILE *probe=_wfopen(large_path.c_str(),L"rb");uint64_t bytes=0;
 bool measured=probe && model_file_size(probe,bytes) && bytes==3095033483ULL && std::fgetc(probe)==42;
 if(probe)std::fclose(probe);
 if(!measured){std::filesystem::remove(large_path);std::cerr<<"64-bit file length/rewind failed\n";return 1;}
 std::mutex large_mutex;std::condition_variable large_cv;bool rejected=false;std::string rejection;
 TranscriptionWorker large_worker;
 large_worker.start(large_path,2,[&](WorkerEvent e){if(e.kind==WorkerEvent::Error){std::lock_guard<std::mutex> lock(large_mutex);rejected=true;rejection=e.text;large_cv.notify_one();}});
 {std::unique_lock<std::mutex> lock(large_mutex);large_cv.wait_for(lock,std::chrono::seconds(30),[&]{return rejected;});}
 large_worker.stop();std::filesystem::remove(large_path);
 if(!rejected || rejection!="ModelInvalid"){std::cerr<<"Large corrupt model not safely rejected\n";return 1;}
 std::cout<<"Sparse >2GB size, rewind and corrupt digest rejection: PASS\n";
#endif
 auto path=std::filesystem::temp_directory_path()/"obs-stt-malformed-header.bin";
 // Correct magic but mismatched audio/text dimensions used to assert in Whisper.
 int32_t header[]={0x67676d6c,51865,1500,512,8,6,448,513,8,6,80,1};
 {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char *>(header),sizeof(header));}
 std::mutex mutex;std::condition_variable cv;bool failed=false;TranscriptionWorker worker;
 worker.start(path,2,[&](WorkerEvent e){if(e.kind==WorkerEvent::Error){std::lock_guard<std::mutex> lock(mutex);failed=true;cv.notify_one();}});
 {std::unique_lock<std::mutex> lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(10),[&]{return failed;})){std::cerr<<"No model error\n";return 1;}}
 worker.stop();
 // An equal-sized corrupt file must be rejected by its digest as well.
 std::filesystem::resize_file(path,147951465);
 failed=false;
 worker.start(path,2,[&](WorkerEvent e){if(e.kind==WorkerEvent::Error){std::lock_guard<std::mutex> lock(mutex);failed=true;cv.notify_one();}});
 {std::unique_lock<std::mutex> lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(10),[&]{return failed;})){std::cerr<<"No hash mismatch error\n";return 1;}}
 worker.stop();std::filesystem::remove(path);
 std::cout<<"Malformed valid-magic model rejected without parser abort: PASS\n";
}
