add_library(lsq_backends STATIC
  src/backend_factory.cpp
  src/device_names.cpp
  fake/fake_backend.cpp
)
add_library(lsq::backends ALIAS lsq_backends)

target_include_directories(lsq_backends PUBLIC include "${CMAKE_CURRENT_SOURCE_DIR}/fake")
target_link_libraries(lsq_backends PUBLIC lsq::core)
find_package(Threads REQUIRED)
target_link_libraries(lsq_backends PUBLIC Threads::Threads)
lsq_set_warnings(lsq_backends)

if(LSQ_BACKEND_MINIAUDIO)
  target_sources(lsq_backends PRIVATE miniaudio/ma_backend.cpp miniaudio/ma_impl.cpp)
  target_include_directories(lsq_backends SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/miniaudio")
  target_include_directories(lsq_backends PRIVATE miniaudio)
  target_compile_definitions(lsq_backends PUBLIC LSQ_HAVE_MINIAUDIO=1)
  target_link_libraries(lsq_backends PUBLIC ${CMAKE_DL_LIBS})
  if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(lsq_backends PUBLIC m)
  endif()
  # miniaudio is third-party code: build its implementation without our warning policy.
  if(MSVC)
    set_source_files_properties(miniaudio/ma_impl.cpp PROPERTIES COMPILE_OPTIONS "/w")
  else()
    set_source_files_properties(miniaudio/ma_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
  endif()
endif()

# Native PipeWire backend (Linux). AUTO builds it when libpipewire-0.3 is installed.
set(_lsq_pw FALSE)
if(UNIX AND NOT APPLE AND NOT LSQ_BACKEND_PIPEWIRE STREQUAL "OFF")
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3>=0.3.40)
  endif()
  if(PIPEWIRE_FOUND)
    set(_lsq_pw TRUE)
  elseif(LSQ_BACKEND_PIPEWIRE STREQUAL "ON")
    message(FATAL_ERROR "LSQ_BACKEND_PIPEWIRE=ON but libpipewire-0.3 (>= 0.3.40) was not found")
  endif()
endif()
if(_lsq_pw)
  message(STATUS "LiveSqueeze: PipeWire backend enabled (libpipewire ${PIPEWIRE_VERSION})")
  target_sources(lsq_backends PRIVATE pipewire/pw_backend.cpp)
  target_include_directories(lsq_backends PUBLIC pipewire)
  target_include_directories(lsq_backends SYSTEM PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
  target_compile_definitions(lsq_backends PUBLIC LSQ_HAVE_PIPEWIRE=1)
  target_link_libraries(lsq_backends PUBLIC ${PIPEWIRE_LIBRARIES})
  target_link_directories(lsq_backends PUBLIC ${PIPEWIRE_LIBRARY_DIRS})
  # The SPA/PipeWire headers use C idioms (compound literals, designated initializers).
  set_source_files_properties(pipewire/pw_backend.cpp PROPERTIES COMPILE_OPTIONS
    "-Wno-pedantic;-Wno-missing-field-initializers;-Wno-cast-align;-Wno-shadow;-Wno-old-style-cast;-Wno-double-promotion;-Wno-narrowing")
else()
  message(STATUS "LiveSqueeze: PipeWire backend disabled")
endif()
