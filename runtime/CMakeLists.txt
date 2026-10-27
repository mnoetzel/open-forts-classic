cmake_minimum_required(VERSION 3.20)
project(openforts-classic-runtime VERSION 0.1.0 LANGUAGES CXX)

# WF.exe is a 32-bit program, so the runtime must be 32-bit as well.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The runtime must be built for 32-bit Windows: cmake -A Win32")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static C++ runtime, so players don't need a Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(ofcrt SHARED
  src/dll-main.cpp
  src/frame-timing.cpp
  src/game-loop.cpp
  src/log.cpp
  src/memory-patch.cpp
  src/widescreen.cpp
)
target_link_libraries(ofcrt PRIVATE user32)
set_target_properties(ofcrt PROPERTIES OUTPUT_NAME "OFCRT")
target_compile_definitions(ofcrt PRIVATE
  OFC_VERSION="${PROJECT_VERSION}"
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
)
target_compile_options(ofcrt PRIVATE /W4 /WX /permissive- /utf-8)
