# P12-MESH-006 -- runs the real `cfdapp --case` binary on a *copy* of a CLI fixture (so nothing is ever
# written into tests/data) and checks the exact exit code and that every given regular expression
# matches the combined stdout/stderr. Used as `cmake -P` from tests/CMakeLists.txt:
#   -DCLI=<cfdapp> -DCASE=<fixture directory> -DWORK=<scratch directory> -DEXPECT_EXIT=<code>
#   -DEXPECT_REGEX=<regex>@@<regex>@@...   ("@@" separates the expressions; a regex may contain '|')
foreach(var CLI CASE WORK EXPECT_EXIT)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "cli_expect.cmake: ${var} is required")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
get_filename_component(name "${CASE}" NAME)
file(COPY "${CASE}" DESTINATION "${WORK}")
file(REMOVE_RECURSE "${WORK}/${name}/results")
execute_process(COMMAND "${CLI}" --case "${WORK}/${name}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(all "${out}${err}")
if(NOT rc EQUAL EXPECT_EXIT)
  message(FATAL_ERROR "cfdapp exit code ${rc}, expected ${EXPECT_EXIT}\n${all}")
endif()
string(REPLACE "@@" ";" expressions "${EXPECT_REGEX}")
foreach(re IN LISTS expressions)
  if(NOT all MATCHES "${re}")
    message(FATAL_ERROR "cfdapp output does not match '${re}'\n${all}")
  endif()
endforeach()
message(STATUS "cfdapp exit ${rc} (expected ${EXPECT_EXIT}); every expected line present\n${all}")
