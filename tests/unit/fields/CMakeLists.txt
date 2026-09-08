add_executable(CFDFieldTests
  test_field.cpp
  test_scalar_field.cpp
  test_vector_field.cpp
  test_surface_field.cpp
  test_field_arithmetic.cpp
)

target_link_libraries(CFDFieldTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDFieldTests)
