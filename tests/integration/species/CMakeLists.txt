# P3-PHYS-004: species transport validation -- pure diffusion (section
# 20), advection-diffusion/Peclet regimes (sections 22-24), conservation
# (sections 18-19), and coupling regressions (sections 25-27), each its
# own binary matching this project's existing per-topic convention (see
# tests/integration/thermal/CMakeLists.txt).
add_executable(CFDSpeciesDiffusionValidationTests
  test_species_diffusion.cpp
)
target_link_libraries(CFDSpeciesDiffusionValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDSpeciesDiffusionValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

add_executable(CFDSpeciesAdvectionDiffusionValidationTests
  test_species_advection_diffusion.cpp
)
target_link_libraries(CFDSpeciesAdvectionDiffusionValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDSpeciesAdvectionDiffusionValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

add_executable(CFDSpeciesConservationValidationTests
  test_species_conservation.cpp
)
target_link_libraries(CFDSpeciesConservationValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDSpeciesConservationValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

add_executable(CFDSpeciesCouplingValidationTests
  test_species_solver_coupling.cpp
)
target_link_libraries(CFDSpeciesCouplingValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDSpeciesCouplingValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
