add_executable(CFDSpeciesTests
  test_species_properties.cpp
  test_species_equation.cpp
  test_species_solver.cpp
  test_species_non_orthogonal.cpp
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
