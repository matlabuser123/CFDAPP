# P3-PHYS-005: two-phase volume-fraction foundation validation --
# uniform-field/zero-velocity preservation, translation/advection +
# grid refinement, phase conservation (closed and open domain), and
# single-phase equivalence / equal-property decoupling / momentum
# (mu_mix) coupling, each its own binary matching this project's
# existing per-topic convention.
add_executable(CFDMultiphaseUniformFieldTests
  test_uniform_alpha.cpp
)
target_link_libraries(CFDMultiphaseUniformFieldTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDMultiphaseUniformFieldTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

add_executable(CFDMultiphaseAdvectionValidationTests
  test_alpha_advection.cpp
)
target_link_libraries(CFDMultiphaseAdvectionValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDMultiphaseAdvectionValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

add_executable(CFDMultiphaseConservationValidationTests
  test_phase_conservation.cpp
)
target_link_libraries(CFDMultiphaseConservationValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDMultiphaseConservationValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

add_executable(CFDMultiphaseSinglePhaseEquivalenceTests
  test_single_phase_equivalence.cpp
)
target_link_libraries(CFDMultiphaseSinglePhaseEquivalenceTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDMultiphaseSinglePhaseEquivalenceTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
