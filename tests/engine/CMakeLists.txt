add_executable(lsq_engine_tests
  main.cpp
  test_frame_ring.cpp
  test_resampler.cpp
  test_drift.cpp
  test_engine.cpp
  test_supervisor.cpp
)
target_link_libraries(lsq_engine_tests PRIVATE lsq::engine lsq::backends lsq_doctest)
target_include_directories(lsq_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core")
lsq_set_warnings(lsq_engine_tests)

add_test(NAME lsq_engine_tests COMMAND lsq_engine_tests)
