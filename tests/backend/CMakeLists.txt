add_executable(lsq_backend_tests
  main.cpp
  test_miniaudio.cpp
)
target_link_libraries(lsq_backend_tests PRIVATE lsq::engine lsq::backends lsq_doctest)
lsq_set_warnings(lsq_backend_tests)

add_test(NAME lsq_backend_tests COMMAND lsq_backend_tests)
