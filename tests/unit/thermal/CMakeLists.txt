add_executable(CFDThermalTests
  test_thermal_properties.cpp
  test_energy_equation.cpp
  test_energy_equation_variable_properties.cpp
  test_thermal_solver.cpp
  test_thermal_solver_variable_properties.cpp
  test_thermal_region.cpp
)

target_link_libraries(CFDThermalTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDThermalTests PROPERTIES LABELS "numerical")
