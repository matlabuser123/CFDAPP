# P0 -- Physical Validation: planar Poiseuille flow vs the closed-form
# analytical solution. The P12-NUM-005 three-grid convergence study
# (GridConvergence, 64x8/96x12/144x18) is enabled (~3 min Debug); the
# per-grid 128x16/256x32 cases are marked DISABLED_ in the test bodies (runtime: minutes, not seconds) -- run
# them explicitly with --gtest_also_run_disabled_tests when regenerating
# full validation evidence, per the file header comment in
# test_poiseuille_validation.cpp.
#
# P12-NUM-007 production validation (test_poiseuille_production_validation.cpp):
# Profile / MassFlow / PressureDrop (64x8) and ProductionGridConvergence
# (64x8/96x12/144x18, writes results/validation/production/poiseuille.json)
# -- all in the default suite.
add_executable(CFDPoiseuilleValidationTests
  PoiseuilleValidationUtils.cpp
  test_poiseuille_validation.cpp
  test_poiseuille_production_validation.cpp
  test_transient_poiseuille.cpp
  test_temporal_refinement.cpp
)

target_link_libraries(CFDPoiseuilleValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

target_include_directories(CFDPoiseuilleValidationTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(CFDPoiseuilleValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
