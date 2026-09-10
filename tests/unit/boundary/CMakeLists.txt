add_executable(CFDBoundaryTests
  test_fixed_value.cpp
  test_fixed_gradient.cpp
  test_wall.cpp
  test_moving_wall.cpp
  test_inlet.cpp
  test_outlet.cpp
  test_symmetry.cpp
  test_boundary_validation.cpp
  test_fixed_temperature.cpp
  test_heat_flux.cpp
  test_adiabatic.cpp
  test_wall_omega.cpp
)

target_link_libraries(CFDBoundaryTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDBoundaryTests PROPERTIES LABELS "unit")
