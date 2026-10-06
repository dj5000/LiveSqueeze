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
option(LSQ_E2E_TESTS "Register the PipeWire end-to-end test (needs pipewire, wireplumber, pw-cat)" OFF)
option(LSQ_BUILD_APP "Build the Qt tray application" OFF)
option(LSQ_BACKEND_MINIAUDIO "Build the miniaudio backend (Windows, macOS, Linux/Pulse)" ON)
set(LSQ_BACKEND_PIPEWIRE "AUTO" CACHE STRING "Build the native PipeWire backend (Linux): AUTO, ON or OFF")
set_property(CACHE LSQ_BACKEND_PIPEWIRE PROPERTY STRINGS AUTO ON OFF)
option(LSQ_WERROR "Treat compiler warnings as errors" OFF)
set(LSQ_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. 'address;undefined' or 'thread'")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(LsqWarnings)
include(LsqSanitizers)

if(LSQ_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(core)
add_subdirectory(backends)
add_subdirectory(engine)

if(LSQ_BUILD_RUN)
  add_subdirectory(apps/run)
endif()

if(LSQ_BUILD_CLI)
  add_subdirectory(cli)
endif()

# Last, so tests can refer to the targets above.
if(LSQ_BUILD_TESTS)
  add_subdirectory(tests)
endif()
