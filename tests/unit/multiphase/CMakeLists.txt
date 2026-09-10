add_executable(CFDMultiphaseTests
  test_phase_properties.cpp
  test_multiphase_properties.cpp
  test_volume_fraction_equation.cpp
  test_volume_fraction_solver.cpp
)

target_link_libraries(CFDMultiphaseTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDMultiphaseTests PROPERTIES LABELS "numerical")
