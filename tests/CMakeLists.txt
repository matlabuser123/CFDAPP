# P0 smoke test, plus unit tests (see tests/unit and PROJECT_STRUCTURE.md).
# numerical/solver/integration/regression tiers are added as their
# corresponding library code lands.

add_test(
  NAME CFDAppSmokeTest
  COMMAND "$<TARGET_FILE:cfdapp>"
)

set_tests_properties(CFDAppSmokeTest PROPERTIES
  PASS_REGULAR_EXPRESSION "CFDApp"
  LABELS "unit"
)

# --- P1 -- Case System CLI integration (TODO.md P1 section 49) -------------
# Exercises the actual `cfdapp --case <dir>` entry point (not just the C++
# CaseReader/CaseBuilder API), against the fixtures in
# tests/data/cases/. WORKING_DIRECTORY is the source tree root so these
# relative case paths resolve the same way a user's own invocation would.
#
# P7-TEST-001: points at tests/data/cases/valid_cavity_cli_smoke, a
# dedicated static copy of valid_cavity's own six input files -- this is
# a raw ctest add_test (a real `cfdapp` process, not a C++ gtest case),
# so it cannot construct a tests/support/CaseFixtureCopy the way every
# gtest-based consumer of valid_cavity now does; a permanent, private
# fixture directory nothing else ever touches achieves the same
# isolation with no moving parts (see tests/support/CaseFixtureCopy.hpp's
# own header comment for the race this closes).
add_test(
  NAME CFDAppCliValidCase
  COMMAND "$<TARGET_FILE:cfdapp>" --case tests/data/cases/valid_cavity_cli_smoke
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliValidCase PROPERTIES
  PASS_REGULAR_EXPRESSION "Converged: yes"
  LABELS "integration"
)

# Each of these fixtures is invalid in one specific way (TODO.md P1
# section 34), except does_not_converge, which is a *valid* case whose
# solver.json (max_iterations: 1) guarantees SIMPLEStatus::MaxIterations
# -- exercising the CLI's other nonzero-exit path (section 32: exit 3,
# distinct from exit 2 for the five actually-invalid cases before it) as
# an end-to-end binary check, not just the exitCodeFor() unit logic. The
# CLI must reject every one of these with a nonzero exit code rather than
# crashing or reporting success. WILL_FAIL makes ctest treat the
# underlying command's nonzero exit as this test PASSING.
foreach(invalid_case IN ITEMS missing_mesh bad_physics bad_boundary bad_solver malformed_json
                              does_not_converge mesh_quality_invalid_cli
                              mesh_quality_disconnected_cli)
  add_test(
    NAME CFDAppCli_${invalid_case}
    COMMAND "$<TARGET_FILE:cfdapp>" --case tests/data/cases/${invalid_case}
    WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  )
  set_tests_properties(CFDAppCli_${invalid_case} PROPERTIES WILL_FAIL TRUE LABELS "integration")
endforeach()

# --- P13-001 -- the CLI's checkpoint flags ---------------------------------
# The binary's own argument handling. The checkpoint/resume ROUND TRIP is
# covered in CI by tests/integration/case/test_checkpoint_production_case.cpp
# (through ProjectRunner, the one execution path); these cover what only a real
# process can: that the flags exist, are documented, and reject bad input
# instead of silently doing nothing.
add_test(
  NAME CFDAppCliCheckpointFlagsAreDocumented
  COMMAND "$<TARGET_FILE:cfdapp>" --help
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliCheckpointFlagsAreDocumented PROPERTIES
  PASS_REGULAR_EXPRESSION "--checkpoint-every"
  LABELS "integration"
)

# Each of these must exit nonzero. A silently-ignored flag is the failure mode
# worth guarding: "--checkpoint-every ten" meaning "never checkpoint" would let
# a production run believe it was protected when it was not.
#   every_without_file  --checkpoint-every without --checkpoint
#   every_not_a_number  a non-integer interval
#   every_zero          an interval of 0
#   resume_missing_arg  --resume with no argument
#   resume_missing_file --resume pointing at a file that does not exist
# Written out one at a time rather than looped: the argument lists contain
# semicolons, which CMake flattens into list separators inside a set(), and the
# first attempt at a loop here produced mangled test names and a broken
# configure. Five explicit blocks cannot be mis-escaped.
function(cfdapp_cli_rejection name)
  add_test(
    NAME CFDAppCliCheckpoint_${name}
    COMMAND "$<TARGET_FILE:cfdapp>" ${ARGN}
    WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  )
  set_tests_properties(CFDAppCliCheckpoint_${name} PROPERTIES
    WILL_FAIL TRUE LABELS "integration")
endfunction()

set(CFDAPP_CLI_SMOKE_CASE tests/data/cases/valid_cavity_cli_smoke)

# --checkpoint-every without --checkpoint: an interval with nothing to write.
cfdapp_cli_rejection(every_without_file --case ${CFDAPP_CLI_SMOKE_CASE} --checkpoint-every 10)
# A non-integer interval. "ten" must not silently mean "never".
cfdapp_cli_rejection(every_not_a_number --case ${CFDAPP_CLI_SMOKE_CASE}
                     --checkpoint x.ckpt --checkpoint-every ten)
# An interval of 0 -- likewise not a silent "never".
cfdapp_cli_rejection(every_zero --case ${CFDAPP_CLI_SMOKE_CASE}
                     --checkpoint x.ckpt --checkpoint-every 0)
# --resume with no argument at all.
cfdapp_cli_rejection(resume_missing_arg --case ${CFDAPP_CLI_SMOKE_CASE} --resume)
# --resume naming a checkpoint that does not exist.
cfdapp_cli_rejection(resume_missing_file --case ${CFDAPP_CLI_SMOKE_CASE}
                     --resume no_such_file.ckpt)

# --- P13-006 -- the package's name and generator list ----------------------
# cfdapp_package_naming() is a pure function of the system name, which is the
# whole reason it is a function: the WINDOWS artifact name is a contract that
# .github/workflows/release.yml globs (CFDApp-*-Windows-x64.zip / .exe) and
# that was verified on real Windows hardware for 0.1.0 and v0.2.0 -- and no
# Windows machine is available to routine testing here. Driving the function
# with `cmake -P` asserts that contract from any host.
#
# Runs without configuring or building anything, so it costs milliseconds and
# is not tied to CFDAPP_ENABLE_PACKAGING being on.
add_test(
  NAME CFDAppPackageNaming
  COMMAND "${CMAKE_COMMAND}" -DCFDAPP_SOURCE_DIR=${CFDApp_SOURCE_DIR}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_package_naming.cmake"
)
set_tests_properties(CFDAppPackageNaming PROPERTIES LABELS "unit")

# An unsupported system must REFUSE rather than invent a plausible name:
# "CFDApp-0.2.0-Haiku-x64.tar.gz" would be a claim that CFDApp runs on Haiku.
# A separate process because a CMake script cannot catch its own FATAL_ERROR.
add_test(
  NAME CFDAppPackageNamingRefusesUnknownSystem
  COMMAND "${CMAKE_COMMAND}" -DCFDAPP_SOURCE_DIR=${CFDApp_SOURCE_DIR}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_package_naming_refuses.cmake"
)
set_tests_properties(CFDAppPackageNamingRefusesUnknownSystem PROPERTIES
  WILL_FAIL TRUE LABELS "unit")

# --- P13-005 -- the CLI's diagnostics channel ------------------------------
# What only a real process can check: that --log-level exists, is documented,
# rejects an unknown level instead of ignoring it, and -- the criterion the
# whole item rests on -- that no log line ever reaches stdout. stdout is the
# report; a diagnostic landing there corrupts a machine-readable run.
add_test(
  NAME CFDAppCliLogLevelIsDocumented
  COMMAND "$<TARGET_FILE:cfdapp>" --help
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliLogLevelIsDocumented PROPERTIES
  PASS_REGULAR_EXPRESSION "--log-level"
  LABELS "integration"
)

# An unknown level must be refused, and the refusal must name the valid ones --
# a user who guessed "verbose" needs to be told what to write instead.
add_test(
  NAME CFDAppCliLogLevelUnknownIsRejected
  COMMAND "$<TARGET_FILE:cfdapp>" --case ${CFDAPP_CLI_SMOKE_CASE} --log-level shout
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliLogLevelUnknownIsRejected PROPERTIES
  WILL_FAIL TRUE LABELS "integration")

# --log-level with no argument at all.
cfdapp_cli_rejection(log_level_missing_arg --case ${CFDAPP_CLI_SMOKE_CASE} --log-level)

# The channel separation itself, at the most verbose level there is: the run
# still prints its report and nothing else. FAIL_REGULAR_EXPRESSION catches a
# bracketed log tag on stdout; PASS_REGULAR_EXPRESSION keeps it from passing by
# printing nothing at all. ctest matches both against stdout+stderr combined,
# so the check runs with stderr discarded -- see the wrapper below.
add_test(
  NAME CFDAppCliLogLinesNeverReachStdout
  COMMAND "${CMAKE_COMMAND}"
          -DCFDAPP=$<TARGET_FILE:cfdapp>
          -DCASE=${CFDAPP_CLI_SMOKE_CASE}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCliStdoutOnly.cmake"
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliLogLinesNeverReachStdout PROPERTIES
  PASS_REGULAR_EXPRESSION "Converged:"
  FAIL_REGULAR_EXPRESSION "\\[(TRACE|DEBUG|INFO|WARNING|ERROR)\\]"
  LABELS "integration"
)

# P12-MESH-004 -- the CLI's mesh-quality report. The two invalid fixtures
# are rejected before any solver runs (they write no results, so their two
# tests each may share the fixture); the error must name the defect and
# where it is. The warning fixture is solved and its report must carry the
# status and the aggregated warning (one test: it writes results/).
add_test(
  NAME CFDAppCliMeshQualityInvalidNamesTheCell
  COMMAND "$<TARGET_FILE:cfdapp>" --case tests/data/cases/mesh_quality_invalid_cli
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliMeshQualityInvalidNamesTheCell PROPERTIES
  PASS_REGULAR_EXPRESSION
    "cell \\(1,0\\) is not a strictly convex counter-clockwise quadrilateral: not convex at corner"
  LABELS "integration"
)
add_test(
  NAME CFDAppCliMeshQualityDisconnectedIsFatal
  COMMAND "$<TARGET_FILE:cfdapp>" --case tests/data/cases/mesh_quality_disconnected_cli
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliMeshQualityDisconnectedIsFatal PROPERTIES
  PASS_REGULAR_EXPRESSION
    "the multiblock mesh is invalid \\(invalid: 32 cells.*fatal connected_components: mesh has 2 disconnected cell regions \\(cells per region: 16, 16\\)"
  LABELS "integration"
)
add_test(
  NAME CFDAppCliMeshQualityWarning
  COMMAND "$<TARGET_FILE:cfdapp>" --case tests/data/cases/mesh_quality_warning_cli
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliMeshQualityWarning PROPERTIES
  PASS_REGULAR_EXPRESSION
    "Mesh quality: valid_with_warnings.*warning expansion_ratio: expansion_ratio 2\\.5 > 2 at face [0-9]+ .*Converged: yes"
  LABELS "integration"
)

# P12-MESH-006 -- the 3D production CLI. tests/cli_expect.cmake runs `cfdapp --case` on a copy of
# the fixture in the build tree (nothing is written into tests/data) and checks the exact exit code
# plus every expected report line ("@@" separates the regular expressions).
set(CFDAPP_CLI_3D_REPORT
    "Mesh quality: valid@@Converged: yes@@U residual: @@V residual: @@W residual: @@P residual: @@Continuity: @@Mass imbalance: @@Face flux: rhie_chow@@Mass balance: inflow [^,]+, outflow [^,]+, relative imbalance @@Cell continuity: max ")
foreach(spec IN ITEMS "valid_duct3d_cli_smoke|Mesh: 12 x 4 x 4 \\(3D\\)"
                      "valid_cube3d_cli_smoke|Mesh: 6 x 6 x 6 \\(3D\\)")
  string(REPLACE "|" ";" parts "${spec}")
  list(GET parts 0 fixture)
  list(GET parts 1 meshLine)
  add_test(
    NAME CFDAppCli3D_${fixture}
    COMMAND "${CMAKE_COMMAND}" "-DCLI=$<TARGET_FILE:cfdapp>"
            "-DCASE=${CFDApp_SOURCE_DIR}/tests/data/cases/${fixture}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli3d/${fixture}" -DEXPECT_EXIT=0
            "-DEXPECT_REGEX=${meshLine}@@${CFDAPP_CLI_3D_REPORT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_expect.cmake")
  set_tests_properties(CFDAppCli3D_${fixture} PROPERTIES LABELS "integration")
endforeach()
# Malformed 3D cases: rejected before any solver runs with exit code 2 (InvalidCase) and a message
# naming the file and the field.
foreach(spec IN ITEMS "invalid_3d_box_without_nz|mesh\\.json: field \"nz\""
                      "invalid_3d_two_component_velocity|boundaries\\.json: field \"patches\\.xmin\\.velocity\\.value\""
                      "invalid_3d_thermal|physics\\.json: field \"thermal\".*3D")
  string(REPLACE "|" ";" parts "${spec}")
  list(GET parts 0 fixture)
  list(GET parts 1 message)
  add_test(
    NAME CFDAppCli3D_${fixture}
    COMMAND "${CMAKE_COMMAND}" "-DCLI=$<TARGET_FILE:cfdapp>"
            "-DCASE=${CFDApp_SOURCE_DIR}/tests/data/cases/${fixture}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli3d/${fixture}" -DEXPECT_EXIT=2
            "-DEXPECT_REGEX=${message}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_expect.cmake")
  set_tests_properties(CFDAppCli3D_${fixture} PROPERTIES LABELS "integration")
endforeach()

# --- GoogleTest -------------------------------------------------------------
# The test surface outgrew plain CHECK-per-line executables once Core grew
# a real exception hierarchy and a stateful singleton Logger (P0 -- Core).
# Fetched once here for the whole tests/ tree; every test target below
# links GTest::gtest_main.
include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# googletest's own install(EXPORT "GTestTargets" ...) rules reject a
# PUBLIC-linked target outside any export set (exactly what
# cfdapp::sanitizers is, below) -- this project never installs anything,
# so skip generating those install rules entirely rather than fight them.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

# TODO.md "P1 -- Quality Gate" section 9-10: under MSVC's ASan, every
# linked translation unit must agree on STL container instrumentation
# (annotate_string/annotate_vector) or the linker rejects the mismatch
# outright (LNK2038) -- googletest is fetched independently of cfdcore,
# so it never picks up cfdapp::sanitizers on its own and needs it applied
# here explicitly, the same way cfdcore already gets it PUBLIC.
if(CFDAPP_ENABLE_SANITIZERS)
  foreach(gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
    if(TARGET ${gtest_target})
      target_link_libraries(${gtest_target} PUBLIC cfdapp::sanitizers)
    endif()
  endforeach()
endif()

include(GoogleTest)

add_subdirectory(unit)
add_subdirectory(solver)
add_subdirectory(integration)
