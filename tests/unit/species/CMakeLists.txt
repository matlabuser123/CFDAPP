add_executable(CFDSpeciesTests
  test_species_properties.cpp
  test_species_equation.cpp
  test_species_solver.cpp
  test_species_non_orthogonal.cpp
  test_mass_fraction_state.cpp
  test_species_transport.cpp
  test_mixture_thermodynamics.cpp
  test_reaction_mechanism.cpp
  test_arrhenius_kinetics.cpp
  test_heat_release.cpp
  test_conservation_validation.cpp
  # G8 closure step 1 -- reaction orders independent of stoichiometry, and the cgs->SI unit
  # conversion of the frozen Westbrook & Dryer Table II Set 3 mechanism.
  # G8 closure step 3 -- adaptive sub-stepping of P18.6 qualified explicit step,
  # gated on refinement against P18.7 independent mpmath reference.
  test_chemistry_integrator.cpp
  test_premixed_flame.cpp
  test_reaction_orders.cpp
  test_device_species_equivalence.cpp
)

target_link_libraries(CFDSpeciesTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-NUM-003: reuses tests/unit/discretization/DistortedMesh.hpp and
# ManufacturedFields.hpp (test-only helpers) rather than duplicating them.
target_include_directories(CFDSpeciesTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../discretization
)

gtest_discover_tests(CFDSpeciesTests PROPERTIES LABELS "numerical")

# TD-035 -- THE SANITIZER FIXTURE, in its OWN target and deliberately NOT in CFDSpeciesTests.
# C7 of TD-004's frozen gate bounds "CFDSpeciesTests excluding G8PremixedFlameTest" at EXACTLY
# 343 tests; adding a test there would make it 344 and break a frozen criterion. Keeping it
# separate also lets ctest run it under the asan preset while the heavy G8 cases are excluded by
# name -- see CMakePresets.json's asan testPreset filter and TD035_sanitizer_scope/audit.md.
add_executable(CFDSpeciesSanitizerTests
  test_premixed_flame_sanitizer.cpp
)

target_link_libraries(CFDSpeciesSanitizerTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDSpeciesSanitizerTests PROPERTIES LABELS "numerical;sanitizer")
