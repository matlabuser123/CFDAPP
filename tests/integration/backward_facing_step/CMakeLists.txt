# P12-NUM-007 -- Production validation: laminar backward-facing step
# (Gartling 1990, ER = 2) vs the published Re = 800 reattachment lengths.
# The geometry is the existing Cartesian mesh with its left boundary split
# into a step wall and per-face inlet patches (BackwardFacingStepCase.hpp)
# -- no new mesh capability. Default suite: Geometry and
# ReattachmentDetection (no solve); the solves (Converges, MassConservation,
# ReattachmentLength -> results/validation/production/
# backward_facing_step.json) are DISABLED_ -- minutes to an hour each; run
# them with --gtest_also_run_disabled_tests in the Release build.
add_executable(CFDBackwardFacingStepTests
  BackwardFacingStepCase.cpp
  test_backward_facing_step.cpp
)

target_link_libraries(CFDBackwardFacingStepTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

target_include_directories(CFDBackwardFacingStepTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(CFDBackwardFacingStepTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
