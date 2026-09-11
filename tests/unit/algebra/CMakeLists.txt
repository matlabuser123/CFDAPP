add_executable(CFDAlgebraTests
  test_vector.cpp
  test_sparse_matrix.cpp
  test_linear_system.cpp
  test_jacobi.cpp
  test_cg.cpp
  test_bicgstab.cpp
  test_solver_failures.cpp
  test_solver_determinism.cpp
  test_linear_solver_factory.cpp
)

target_link_libraries(CFDAlgebraTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDAlgebraTests PROPERTIES LABELS "unit")
