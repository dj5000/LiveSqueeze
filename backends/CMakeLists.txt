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
