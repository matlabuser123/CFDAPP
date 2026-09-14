# P0 -- Physical Validation: lid-driven cavity vs Ghia et al. (1982).
# 40x40/80x80 and the P12-NUM-005 20/40/80 GridConvergence study are
# marked DISABLED_ in the test
# bodies (runtime: minutes, not seconds) -- run them explicitly with
# --gtest_also_run_disabled_tests when regenerating full validation
# evidence, per the file header comment in test_cavity_ghia.cpp.
#
# P12-NUM-007 production validation (CavityProductionCase.cpp):
# test_cavity_production_validation.cpp -- Re = 100 / 1000 vs Ghia; only
# CavityGhiaValidation.Re100Grid20 is in the default suite, every other
# grid and the cavity_re100 / cavity_re1000 studies are DISABLED_ (run
# explicitly, Release for 160x160). test_scheme_validation.cpp -- the four
# convection schemes on 20x20 (default suite) and the full accuracy / cost
# comparison (DISABLED_AccuracyCostComparisonFull). Reports:
# results/validation/production/.
add_executable(CFDCavityValidationTests
  CavityValidationUtils.cpp
  CavityProductionCase.cpp
  test_cavity_ghia.cpp
  test_cavity_production_validation.cpp
  test_scheme_validation.cpp
  test_transient_cavity.cpp
)

target_link_libraries(CFDCavityValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

target_include_directories(CFDCavityValidationTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(CFDCavityValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
