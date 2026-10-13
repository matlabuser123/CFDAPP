add_executable(CFDTurbulenceTests
  test_turbulence_model.cpp
  test_laminar_model.cpp
  test_kepsilon_coefficients.cpp
  test_turbulence_production.cpp
  test_kepsilon_equation.cpp
  test_kepsilon_model.cpp
  test_komega_coefficients.cpp
  test_komega_equation.cpp
  test_komega_model.cpp
  test_sst_coefficients.cpp
  test_wall_distance.cpp
  # P16: the near-wall treatment, round-tripped against the log law.
  test_wall_functions.cpp
  test_wall_treatment.cpp
  # P16: proof the wall treatment reaches the k-epsilon and momentum assemblies.
  test_wall_function_wiring.cpp
  # P16: SST on the shared wall treatment, plus its SST-specific controls.
  test_sst_wall_functions.cpp
  # P16b: Menter automatic near-wall treatment, incl. the zero-shear proof.
  test_automatic_wall_treatment.cpp
  # P16b: Spalding composite law and the two-scale momentum wall flux.
  test_spalding_wall_law.cpp
  test_momentum_wall_flux.cpp
  # P14: the bitwise gate for migrating turbulence assembly onto cfd::transport.
  test_p14_migration_equivalence.cpp
  test_sst_equation.cpp
  test_sst_model.cpp
  test_turbulence_model_factory.cpp
  test_turbulence_non_orthogonal.cpp
)

target_link_libraries(CFDTurbulenceTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-NUM-003: reuses tests/unit/discretization/DistortedMesh.hpp and
# ManufacturedFields.hpp (test-only helpers) rather than duplicating them.
target_include_directories(CFDTurbulenceTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../discretization
)

gtest_discover_tests(CFDTurbulenceTests PROPERTIES LABELS "unit")
