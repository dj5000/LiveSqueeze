cmake_minimum_required(VERSION 3.25)

project(LiveSqueeze
  VERSION 0.1.0
  DESCRIPTION "Realtime audio compressor and limiter with a virtual audio device front end"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LSQ_BUILD_TESTS "Build unit tests" ON)
option(LSQ_BUILD_CLI "Build the offline lsq-cli tool" ON)
option(LSQ_BUILD_RUN "Build the headless lsq-run live runner" ON)
option(LSQ_BUILD_APP "Build the Qt tray application" OFF)
option(LSQ_BACKEND_MINIAUDIO "Build the miniaudio backend (Windows, macOS, Linux/Pulse)" ON)
option(LSQ_BACKEND_PIPEWIRE "Build the native PipeWire backend (Linux)" OFF)
option(LSQ_WERROR "Treat compiler warnings as errors" OFF)
set(LSQ_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. 'address;undefined' or 'thread'")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(LsqWarnings)
include(LsqSanitizers)

add_subdirectory(core)
add_subdirectory(backends)
add_subdirectory(engine)

if(LSQ_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LSQ_BUILD_RUN)
  add_subdirectory(apps/run)
endif()

if(LSQ_BUILD_CLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli/CMakeLists.txt")
  add_subdirectory(cli)
endif()
