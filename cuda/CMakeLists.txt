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

# Target architectures. cmake/CUDA.cmake picks them from the toolkit's own
# capability before enable_language(CUDA) -- it has to happen there, because
# enable_language initialises this variable from nvcc's default (sm_52) and a
# "set it if empty" here can never fire (CUDA-QUAL-001, 2026-09-18). CUDA
# >= 11.8 gives "80;89": cubin native to both Ampere and the Ada development
# GPU, each with its compute_NN PTX for forward compatibility; older toolkits,
# which cannot name sm_89, give "80". Override with
# -DCMAKE_CUDA_ARCHITECTURES=... or CUDAARCHS for a different target GPU.
message(STATUS "cfdcuda: CUDA ${CMAKE_CUDA_COMPILER_VERSION}, architectures ${CMAKE_CUDA_ARCHITECTURES}")
set_target_properties(cfdcuda PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")

target_link_libraries(cfdcuda
  PUBLIC
    CUDA::cudart
)
