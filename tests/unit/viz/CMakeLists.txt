add_executable(CFDVizTests
  test_marching_squares.cpp
  test_derived_fields.cpp
  test_field_probe.cpp
  test_vector_sampling.cpp
)

target_link_libraries(CFDVizTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDVizTests PROPERTIES LABELS "unit")
