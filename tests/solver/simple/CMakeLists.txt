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
)

target_link_libraries(CFDSimpleTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDSimpleTests PROPERTIES LABELS "solver")
