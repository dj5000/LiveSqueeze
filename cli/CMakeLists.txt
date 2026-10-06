add_executable(lsq-cli
  main.cpp
  signals.cpp
  wav_io.cpp
  analyze.cpp
)
target_link_libraries(lsq-cli PRIVATE lsq::core)
target_include_directories(lsq-cli PRIVATE "${PROJECT_SOURCE_DIR}/third_party/dr_libs")
lsq_set_warnings(lsq-cli)
# dr_wav is third-party code; keep its warnings out of our -Werror policy.
set_source_files_properties(wav_io.cpp PROPERTIES COMPILE_OPTIONS
  "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-shadow;-Wno-double-promotion;-Wno-pedantic;-Wno-cast-align;-Wno-format-nonliteral>")
install(TARGETS lsq-cli RUNTIME DESTINATION bin)

if(LSQ_BUILD_TESTS)
  add_test(NAME lsq_cli_roundtrip
    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:lsq-cli>
            -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/roundtrip
            -P ${PROJECT_SOURCE_DIR}/tests/cli/roundtrip.cmake)
endif()
