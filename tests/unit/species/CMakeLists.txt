add_executable(CFDSpeciesTests
  test_species_properties.cpp
  test_species_equation.cpp
  test_species_solver.cpp
)

target_link_libraries(CFDSpeciesTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDSpeciesTests PROPERTIES LABELS "numerical")
