add_executable(cfd_benchmark_gpu_spmv
  benchmark_gpu_spmv.cpp
)

target_link_libraries(cfd_benchmark_gpu_spmv
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
)

# P6-GPU-003 -- Performance: baseline + Jacobi-preconditioner comparison,
# both at the raw linear-system level and against a full production
# SIMPLE lid-driven-cavity solve. Safe to build (and prints a short
# "nothing to benchmark" message) regardless of CUDA/device availability
# -- see the source file's own header comment.
add_executable(cfd_benchmark_gpu_preconditioner
  benchmark_gpu_preconditioner.cpp
)

target_link_libraries(cfd_benchmark_gpu_preconditioner
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    nlohmann_json::nlohmann_json
)

# P7-PERF-001 -- Performance: real end-to-end CPU-vs-GPU production solve
# timing (whole SIMPLE::solve(), never kernel-only timings). Safe to
# build (and prints a short "nothing to benchmark" message) regardless of
# device availability -- see the source file's own header comment.
add_executable(cfd_benchmark_cuda_end_to_end
  benchmark_cuda_end_to_end.cpp
)

target_link_libraries(cfd_benchmark_cuda_end_to_end
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    nlohmann_json::nlohmann_json
)

# P7-PERF-003 -- Performance: large-grid CPU/GPU stress testing (memory,
# stability, NaN/Inf, mass conservation, convergence trends). Needs
# cudaMemGetInfo (cuda_runtime.h) directly, so -- like every other file
# in this directory -- only built when CFDAPP_ENABLE_CUDA=ON.
add_executable(cfd_benchmark_large_grid_stress
  benchmark_large_grid_stress.cpp
)

target_link_libraries(cfd_benchmark_large_grid_stress
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    nlohmann_json::nlohmann_json
    CUDA::cudart
)
