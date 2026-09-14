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
  test_momentum_non_orthogonal.cpp
)

target_link_libraries(CFDPhysicsTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-NUM-003: reuses tests/unit/discretization/DistortedMesh.hpp and
# ManufacturedFields.hpp (test-only helpers) rather than duplicating them.
target_include_directories(CFDPhysicsTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../discretization
)

gtest_discover_tests(CFDPhysicsTests PROPERTIES LABELS "numerical")
