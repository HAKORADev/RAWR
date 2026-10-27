cmake_minimum_required(VERSION 3.16)
project(rawr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
cmake_policy(SET CMP0091 NEW)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The runtime library has to be chosen before any target is created, or the
# static library and the executable end up with different runtimes and the
# linker complains about multiply defined symbols.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(RAWR_STATIC "link the runtime statically so the binary stands alone" ON)

set(RAWR_CORE_SOURCES
    src/core/cli.cpp
    src/core/crc32.cpp
    src/core/fsutil.cpp
    src/core/pe.cpp
    src/core/rwg.cpp
    src/core/selftest.cpp
    src/core/stream_cipher.cpp
)

add_library(rawr_core STATIC ${RAWR_CORE_SOURCES})
target_include_directories(rawr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

if(MSVC)
  add_executable(rawr src/windows/main.cpp src/windows/platform_win32.cpp)
  target_compile_options(rawr PRIVATE /EHsc /utf-8 /W3)
  if(RAWR_STATIC)
    target_compile_definitions(rawr PRIVATE RAWR_STATIC_BUILD=1)
  endif()
else()
  add_executable(rawr src/linux/main.cpp src/linux/platform_posix.cpp)
  target_compile_options(rawr PRIVATE -O2 -Wall -Wextra)
  if(RAWR_STATIC)
    target_compile_definitions(rawr PRIVATE RAWR_STATIC_BUILD=1)
    target_link_options(rawr PRIVATE -static)
  endif()
endif()

target_link_libraries(rawr PRIVATE rawr_core)

if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(rawr PRIVATE Threads::Threads)
endif()

install(TARGETS rawr RUNTIME DESTINATION .)
