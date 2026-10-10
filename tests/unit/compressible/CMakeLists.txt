add_executable(CFDCompressibleTests
  test_ideal_gas_eos.cpp
  test_thermodynamic_properties.cpp
  test_compressible_mass_flux.cpp
  test_compressible_continuity.cpp
  test_compressible_momentum.cpp
  test_total_energy.cpp
  # P17.7 -- the HLLC flux. hllc_reference_data.hpp beside it is GENERATED from the frozen
  # results/p17/hllc/tools/exact_riemann.py (mpmath, 60 digits, no CFDApp code), which the frozen
  # G6(f) criterion requires as an INDEPENDENT reference.
  test_hllc_flux.cpp
  # P17.8 -- the density-based compressible solver: state conversion, residual assembly,
  # owner/neighbour cancellation, CFL (including the tie-back to the EXISTING solver::calculateCFL),
  # the update, both stepping modes, the canonical cases, conservation, robustness and determinism.
  test_density_based_solver.cpp
  # P17.10 -- the cross-comparison authorization section 4 requires for a restated formula:
  # include/cfd/gpu/DeviceThermodynamics.hpp against production, BITWISE. It lives here rather
  # than under tests/unit/gpu so it also runs with CUDA OFF -- the shared header is host/device
  # and compiles into the CPU build unchanged.
  test_device_thermodynamics_equivalence.cpp
)

target_link_libraries(CFDCompressibleTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
# so test_hllc_flux.cpp can include its generated reference data by plain name
target_include_directories(CFDCompressibleTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(CFDCompressibleTests PROPERTIES LABELS "numerical")
