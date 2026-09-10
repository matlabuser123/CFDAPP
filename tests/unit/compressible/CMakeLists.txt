add_executable(CFDCompressibleTests
  test_ideal_gas_eos.cpp
  test_thermodynamic_properties.cpp
  test_compressible_mass_flux.cpp
  test_compressible_continuity.cpp
  test_compressible_momentum.cpp
)

target_link_libraries(CFDCompressibleTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDCompressibleTests PROPERTIES LABELS "numerical")
