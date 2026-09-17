add_executable(CFDDiscretizationTests
  test_interpolation.cpp
  test_gradient.cpp
  test_divergence.cpp
  test_laplacian.cpp
  test_diffusion.cpp
  test_convection.cpp
  test_grid_refinement.cpp
  test_time_derivative.cpp
  test_vector_gradient.cpp
  test_oblique_neumann_gradient.cpp
  test_operators3d.cpp
  # P12-MESH-007: ALE operators (moving-mesh time derivative, relative flux).
  test_ale_operators.cpp
  test_boundary_reconstruction.cpp
  # P12-GRAD-002 A2 (C13): continuous Green-Gauss boundary-gradient regression tests.
  test_gradient_boundary_consistency.cpp
)

target_link_libraries(CFDDiscretizationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-MESH-007: tests/support/MeshMotionCases.hpp (the gate configurations).
target_include_directories(CFDDiscretizationTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../support
)

gtest_discover_tests(CFDDiscretizationTests PROPERTIES LABELS "numerical")
