add_executable(cfd_benchmark_gpu_spmv
  benchmark_gpu_spmv.cpp
)

target_link_libraries(cfd_benchmark_gpu_spmv
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
)
