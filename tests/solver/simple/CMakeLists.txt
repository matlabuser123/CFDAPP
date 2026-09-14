add_executable(CFDSimpleTests
  test_simple_settings.cpp
  test_momentum_predictor.cpp
  test_pressure_correction.cpp
  test_velocity_correction.cpp
  test_flux_correction.cpp
  test_simple_continuity.cpp
  test_simple_convergence.cpp
  test_simple_failure.cpp
  test_simple_nonfinite.cpp
  test_simple_determinism.cpp
  test_simple_turbulence.cpp
  test_simple_kepsilon.cpp
  test_simple_komega.cpp
  test_simple_sst.cpp
  test_simple_turbulence_models.cpp
  test_simple_buoyancy.cpp
  test_simple_gpu_residency.cpp
  test_simple_gpu_solver.cpp
  test_simple_non_orthogonal.cpp
  test_pressure_correction_non_orthogonal.cpp
  test_simple_robustness.cpp
)

target_link_libraries(CFDSimpleTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-NUM-003: reuses tests/unit/discretization/DistortedMesh.hpp and
# ManufacturedFields.hpp (test-only helpers) rather than duplicating them.
target_include_directories(CFDSimpleTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../unit/discretization
)

gtest_discover_tests(CFDSimpleTests PROPERTIES LABELS "solver")
