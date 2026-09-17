add_executable(CFDAlgebraTests
  test_vector.cpp
  test_sparse_matrix.cpp
  test_linear_system.cpp
  test_jacobi.cpp
  test_cg.cpp
  test_bicgstab.cpp
  test_bicgstab_breakdown.cpp
  test_solver_failures.cpp
  test_solver_determinism.cpp
  test_linear_solver_factory.cpp
  test_gmres.cpp
  test_linear_solver_fallback.cpp
)

target_link_libraries(CFDAlgebraTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-MESH-004: test_bicgstab_breakdown.cpp reads the stored reproducer
# systems from tests/data/linear_systems/.
target_compile_definitions(CFDAlgebraTests
  PRIVATE CFDAPP_TEST_DATA_DIR="${CFDApp_SOURCE_DIR}/tests/data")

gtest_discover_tests(CFDAlgebraTests PROPERTIES LABELS "unit")
