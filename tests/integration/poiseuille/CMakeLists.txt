# P0 -- Physical Validation: planar Poiseuille flow vs the closed-form
# analytical solution. 128x16/256x32/grid-refinement cases are marked
# DISABLED_ in the test bodies (runtime: minutes, not seconds) -- run
# them explicitly with --gtest_also_run_disabled_tests when regenerating
# full validation evidence, per the file header comment in
# test_poiseuille_validation.cpp.
add_executable(CFDPoiseuilleValidationTests
  PoiseuilleValidationUtils.cpp
  test_poiseuille_validation.cpp
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
)
