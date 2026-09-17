add_executable(CFDThermalTests
  test_thermal_properties.cpp
  test_energy_equation.cpp
  test_energy_equation_variable_properties.cpp
  test_thermal_solver.cpp
  test_thermal_solver_variable_properties.cpp
  test_thermal_region.cpp
  test_thermal_non_orthogonal.cpp
  test_thermal_boundary_consistency.cpp
  test_conjugate_boundary_equivalence.cpp
  test_sparse_assembly3d.cpp
)

target_link_libraries(CFDThermalTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-NUM-003: reuses tests/unit/discretization/DistortedMesh.hpp and
# ManufacturedFields.hpp (test-only helpers) rather than duplicating them.
target_include_directories(CFDThermalTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../discretization
    # P12-DIFF-002 validation migration: tests/support/BoundaryFluxProbe.hpp, the one shared
    # "flux production actually assembles" diagnostic used by the conservation tests.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../support
)

gtest_discover_tests(CFDThermalTests PROPERTIES LABELS "numerical")
