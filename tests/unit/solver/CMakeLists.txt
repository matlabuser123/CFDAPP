add_executable(CFDSolverTests
  test_time_controller.cpp
  test_cfl.cpp
  test_transient_solver.cpp
)

target_link_libraries(CFDSolverTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDSolverTests PROPERTIES LABELS "unit")
