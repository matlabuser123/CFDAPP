add_executable(cfd_benchmark_cpu
  benchmark_runner.cpp
)

target_link_libraries(cfd_benchmark_cpu
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    nlohmann_json::nlohmann_json
)

# P4 -- Performance, OpenMP scaling: safe to build (and a no-op to run)
# regardless of CFDAPP_ENABLE_OPENMP -- see the source file's own header
# comment.
add_executable(cfd_benchmark_spmv_scaling
  benchmark_spmv_scaling.cpp
)

target_link_libraries(cfd_benchmark_spmv_scaling
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
)

# P7-PERF-002 -- Performance: end-to-end production-SIMPLE OpenMP
# thread-count scaling (never SpMV-only -- see benchmark_spmv_scaling.cpp
# for that microbenchmark). Safe to build/run regardless of
# CFDAPP_ENABLE_OPENMP -- see the source file's own header comment.
add_executable(cfd_benchmark_openmp_scaling
  benchmark_openmp_scaling.cpp
)

target_link_libraries(cfd_benchmark_openmp_scaling
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    nlohmann_json::nlohmann_json
)
