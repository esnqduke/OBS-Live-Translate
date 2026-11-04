cmake_minimum_required(VERSION 3.28...3.30)

option(TRANSCRIPTION_TESTS_ONLY "Build native tests without OBS SDK" OFF)
if(TRANSCRIPTION_TESTS_ONLY)
  project(transcription-tests LANGUAGES C CXX)
  enable_testing()
  include(cmake/whisper.cmake)
  add_subdirectory(tests)
  return()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/caption-source.cpp src/caption-source.hpp)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

include(cmake/whisper.cmake)
add_library(transcription STATIC src/audio-resampler.cpp src/transcription-worker.cpp)
target_compile_features(transcription PUBLIC cxx_std_17)
target_include_directories(transcription PUBLIC src)
target_link_libraries(transcription PUBLIC whisper)
if(WIN32)
  target_link_libraries(transcription PRIVATE bcrypt)
endif()
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE transcription)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/transcription-filter.cpp)
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
