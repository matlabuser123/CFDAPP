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
  test_sst_equation.cpp
  test_sst_model.cpp
  test_turbulence_model_factory.cpp
)

target_link_libraries(CFDTurbulenceTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDTurbulenceTests PROPERTIES LABELS "unit")
