# Runs lsq-cli end to end: generate a 5.1 file, process it, analyze the result.
# usage: cmake -DCLI=<path to lsq-cli> -DWORKDIR=<scratch dir> -P roundtrip.cmake
file(MAKE_DIRECTORY "${WORKDIR}")
set(IN "${WORKDIR}/in51.wav")
set(OUT "${WORKDIR}/out.wav")

function(run_cli)
  execute_process(COMMAND "${CLI}" ${ARGN}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "lsq-cli ${ARGN} failed (${rc}):\n${out}\n${err}")
  endif()
  set(CLI_OUTPUT "${out}" PARENT_SCOPE)
endfunction()

run_cli(gen --kind movie --layout 5.1 --seconds 8 --out "${IN}")
run_cli(process "${IN}" "${OUT}" --preset late-night --set ceiling_db=-3)
if(NOT CLI_OUTPUT MATCHES "FL FR FC LFE BL BR -> stereo")
  message(FATAL_ERROR "process did not report a 5.1 to stereo downmix:\n${CLI_OUTPUT}")
endif()

run_cli(analyze "${OUT}")
if(NOT CLI_OUTPUT MATCHES "2 channels")
  message(FATAL_ERROR "output is not stereo:\n${CLI_OUTPUT}")
endif()

# The late-night preset has a -3 dBTP ceiling: the processed file must not exceed it by much.
string(REGEX MATCH "ch0 FL +sample peak +([-0-9.]+)" _ "${CLI_OUTPUT}")
if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 GREATER -2.5)
  message(FATAL_ERROR "sample peak ${CMAKE_MATCH_1} dBFS is above the -3 dB ceiling:\n${CLI_OUTPUT}")
endif()

# Bad input must fail cleanly rather than crash.
execute_process(COMMAND "${CLI}" process "${WORKDIR}/does-not-exist.wav" "${OUT}"
  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "processing a missing file should fail")
endif()
execute_process(COMMAND "${CLI}" process "${IN}" "${OUT}" --set bogus=1
  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "an unknown parameter should fail")
endif()
message(STATUS "lsq-cli round trip OK")
