# P13-005 -- run cfdapp at the most verbose log level and echo ONLY its stdout.
#
# Two things this wrapper exists for:
#
# 1. ctest matches PASS/FAIL_REGULAR_EXPRESSION against the child's stdout and
#    stderr COMBINED, which is exactly the distinction this test is about.
#    Capturing the two separately and re-emitting only stdout is what lets
#    FAIL_REGULAR_EXPRESSION mean "a log line reached stdout" rather than "a
#    log line was emitted at all" -- at --log-level trace, plenty are.
#
# 2. The run is given its own copy of the case. The CLI smoke fixture is shared
#    with CFDAppCliValidCase, and two processes exporting into one <case>/results
#    would race.
get_filename_component(case_name "${CASE}" NAME)
set(private_case "${CMAKE_CURRENT_BINARY_DIR}/cli_log_level_${case_name}")
file(REMOVE_RECURSE "${private_case}")
file(COPY "${CASE}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(RENAME "${CMAKE_CURRENT_BINARY_DIR}/${case_name}" "${private_case}")

execute_process(
  COMMAND "${CFDAPP}" --case "${private_case}" --log-level trace
  OUTPUT_VARIABLE cli_stdout
  ERROR_VARIABLE cli_stderr
  RESULT_VARIABLE cli_status
)
string(LENGTH "${cli_stderr}" stderr_bytes)
message(STATUS "cfdapp exited with ${cli_status}; ${stderr_bytes} bytes of stderr withheld "
               "from the match on purpose")
message("${cli_stdout}")
