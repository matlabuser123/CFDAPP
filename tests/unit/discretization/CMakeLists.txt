add_executable(CFDDiscretizationTests
  test_interpolation.cpp
  test_gradient.cpp
  test_divergence.cpp
  test_laplacian.cpp
  test_diffusion.cpp
  test_convection.cpp
  test_grid_refinement.cpp
  test_time_derivative.cpp
)

target_link_libraries(CFDDiscretizationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDDiscretizationTests PROPERTIES LABELS "numerical")
