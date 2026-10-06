add_library(lsq_core STATIC
  src/channel_map.cpp
  src/compressor.cpp
  src/downmix.cpp
  src/dynamics_chain.cpp
  src/limiter.cpp
  src/params.cpp
  src/presets.cpp
  src/true_peak.cpp
  src/version.cpp
)
add_library(lsq::core ALIAS lsq_core)

target_include_directories(lsq_core PUBLIC include)
target_compile_features(lsq_core PUBLIC cxx_std_20)
lsq_set_warnings(lsq_core)
