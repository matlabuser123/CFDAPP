# P0 smoke test, plus unit tests (see tests/unit and PROJECT_STRUCTURE.md).
# numerical/solver/integration/regression tiers are added as their
# corresponding library code lands.

add_test(
  NAME CFDAppSmokeTest
  COMMAND "$<TARGET_FILE:cfdapp>"
)

set_tests_properties(CFDAppSmokeTest PROPERTIES
  PASS_REGULAR_EXPRESSION "CFDApp"
)

# --- P1 -- Case System CLI integration (TODO.md P1 section 49) -------------
# Exercises the actual `cfdapp --case <dir>` entry point (not just the C++
# CaseReader/CaseBuilder API), against the fixtures in
# tests/data/cases/. WORKING_DIRECTORY is the source tree root so these
# relative case paths resolve the same way a user's own invocation would.
add_test(
  NAME CFDAppCliValidCase
  COMMAND "$<TARGET_FILE:cfdapp>" --case tests/data/cases/valid_cavity
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
)
set_tests_properties(CFDAppCliValidCase PROPERTIES
  PASS_REGULAR_EXPRESSION "Converged: yes"
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
                              does_not_converge)
  add_test(
    NAME CFDAppCli_${invalid_case}
    COMMAND "$<TARGET_FILE:cfdapp>" --case tests/data/cases/${invalid_case}
    WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  )
  set_tests_properties(CFDAppCli_${invalid_case} PROPERTIES WILL_FAIL TRUE)
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
