add_library(lsq_engine STATIC
  src/async_resampler.cpp
  src/engine.cpp
  src/supervisor.cpp
)
add_library(lsq::engine ALIAS lsq_engine)

target_include_directories(lsq_engine PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(lsq_engine PUBLIC lsq::core lsq::backends Threads::Threads)
lsq_set_warnings(lsq_engine)
