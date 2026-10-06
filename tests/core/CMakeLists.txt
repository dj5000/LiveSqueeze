add_executable(lsq_core_tests
  main.cpp
  test_smoke.cpp
)
target_link_libraries(lsq_core_tests PRIVATE lsq::core lsq_doctest)
lsq_set_warnings(lsq_core_tests)

add_test(NAME lsq_core_tests COMMAND lsq_core_tests)
