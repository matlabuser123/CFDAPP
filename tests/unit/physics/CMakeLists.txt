add_executable(CFDPhysicsTests
  test_fluid_properties.cpp
  test_mass_flux.cpp
  test_continuity_equation.cpp
  test_momentum_diffusion.cpp
  test_momentum_convection.cpp
  test_momentum_pressure.cpp
  test_momentum_assembly.cpp
  test_boussinesq_buoyancy.cpp
  test_temperature_property.cpp
  test_momentum_variable_viscosity.cpp
)

target_link_libraries(CFDPhysicsTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDPhysicsTests PROPERTIES LABELS "numerical")
