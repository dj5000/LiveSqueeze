add_executable(lsq_core_tests
  main.cpp
  alloc_counter.cpp
  test_chain.cpp
  test_channel_map.cpp
  test_compressor.cpp
  test_downmix.cpp
  test_gain_computer.cpp
  test_limiter.cpp
  test_params.cpp
  test_smoke.cpp
  test_threads.cpp
)
target_link_libraries(lsq_core_tests PRIVATE lsq::core lsq_doctest)
find_package(Threads REQUIRED)
target_link_libraries(lsq_core_tests PRIVATE Threads::Threads)
lsq_set_warnings(lsq_core_tests)

add_test(NAME lsq_core_tests COMMAND lsq_core_tests)
