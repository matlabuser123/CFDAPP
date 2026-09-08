# P0 -- Physical Validation: lid-driven cavity vs Ghia et al. (1982).
# 40x40/80x80/grid-refinement cases are marked DISABLED_ in the test
# bodies (runtime: minutes, not seconds) -- run them explicitly with
# --gtest_also_run_disabled_tests when regenerating full validation
# evidence, per the file header comment in test_cavity_ghia.cpp.
add_executable(CFDCavityValidationTests
  CavityValidationUtils.cpp
  test_cavity_ghia.cpp
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
