# Included only when CFDAPP_ENABLE_CUDA=ON (root CMakeLists.txt already
# called enable_language(CUDA) via cmake/CUDA.cmake by the time this
# runs). Builds the cfdcuda library -- currently just the CSR SpMV
# kernel (P4 -- Performance, section 32's own narrow "first CUDA target").
add_library(cfdcuda STATIC
  kernels/CsrSpmvKernel.cu
  kernels/GpuResidencyManagerCuda.cpp
  kernels/DeviceVectorOpsKernel.cu
  kernels/GpuPreconditionerKernel.cu
  kernels/GpuLinearSolverCuda.cpp
)
add_library(cfdapp::cuda ALIAS cfdcuda)

target_include_directories(cfdcuda
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/include>"
)

# Ampere (sm_80) as a virtual/forward-compatible target: this project's
# own nvcc (CUDA 11.5) predates official Ada (sm_89) support, but the
# CUDA driver's own PTX JIT forward-compatibility guarantee lets an
# sm_80 PTX payload run on later architectures (verified on this
# project's own development GPU, an RTX 5000 Ada, compute capability
# 8.9 -- see TODO.md's own P4 status note). Override with
# -DCMAKE_CUDA_ARCHITECTURES=... for a different target GPU generation.
if(NOT CMAKE_CUDA_ARCHITECTURES)
  set(CMAKE_CUDA_ARCHITECTURES 80)
endif()
set_target_properties(cfdcuda PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")

target_link_libraries(cfdcuda
  PUBLIC
    CUDA::cudart
)
