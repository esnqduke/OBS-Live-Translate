include(FetchContent)
# Static engines; optional Vulkan uses the installed system GPU driver.
option(ENABLE_WHISPER_VULKAN "Build local Vulkan GPU backend" OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(flag WHISPER_BUILD_TESTS WHISPER_BUILD_EXAMPLES WHISPER_BUILD_SERVER
    WHISPER_CURL WHISPER_COREML WHISPER_OPENVINO GGML_CUDA
    GGML_METAL GGML_SYCL GGML_RPC GGML_BLAS GGML_OPENMP GGML_NATIVE GGML_BACKEND_DL)
  set(${flag} OFF CACHE BOOL "" FORCE)
endforeach()
set(GGML_VULKAN ${ENABLE_WHISPER_VULKAN} CACHE BOOL "" FORCE)
if(ENABLE_WHISPER_VULKAN)
  include(ExternalProject)
  # Keep MSBuild's nested shader-generator tracking paths below MAX_PATH.
  set_property(DIRECTORY PROPERTY EP_BASE "${CMAKE_BINARY_DIR}/ep")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_Declare(whisper
  URL https://codeload.github.com/ggml-org/whisper.cpp/tar.gz/refs/tags/v1.7.6
  URL_HASH SHA256=166140e9a6d8a36f787a2bd77f8f44dd64874f12dd8359ff7c1f4f9acb86202e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(whisper)
# OBS template enables /WX globally; upstream 1.7.6 has conversion warnings
# with v145. Keep /WX for our code, do not promote dependency warnings.
foreach(dependency whisper ggml ggml-base ggml-cpu ggml-vulkan)
  if(TARGET ${dependency})
    set_property(TARGET ${dependency} PROPERTY COMPILE_WARNING_AS_ERROR OFF)
  endif()
endforeach()
