add_executable(CFDGpuTests
  test_cuda_spmv.cpp
  test_device_buffer.cpp
  test_gpu_residency_manager.cpp
  test_gpu_linear_solver.cpp
)

target_link_libraries(CFDGpuTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDGpuTests PROPERTIES LABELS "numerical")
