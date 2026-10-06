add_library(lsq_doctest INTERFACE)
target_include_directories(lsq_doctest SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party/doctest")

add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(backend)

# End-to-end test of the PipeWire backend on a headless PipeWire. Opt-in because it needs the
# PipeWire tools and takes about a minute. Run with: ctest -L e2e
if(LSQ_E2E_TESTS AND TARGET lsq-run AND TARGET lsq-cli AND UNIX AND NOT APPLE)
  add_test(NAME lsq_pipewire_e2e
    COMMAND ${PROJECT_SOURCE_DIR}/scripts/e2e/run_pw_e2e.sh ${CMAKE_BINARY_DIR})
  set_tests_properties(lsq_pipewire_e2e PROPERTIES LABELS e2e SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
