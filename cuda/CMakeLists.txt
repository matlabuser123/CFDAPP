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
  # GPU-DISC-001A: the immutable mesh, mirrored once into device memory.
  kernels/DeviceMeshCuda.cpp
  # GPU-DISC-001B: the Green-Gauss gradient -- host-built immutable plan plus
  # the field-dependent kernels.
  kernels/DeviceGradientPlan.cpp
  kernels/DeviceGradientKernel.cu
  # GPU-DISC-001C: the implicit diffusion assembly.
  kernels/DeviceDiffusionPlan.cpp
  kernels/DeviceDiffusionKernel.cu
  # GPU-DISC-001D: the convection schemes.
  kernels/DeviceConvectionPlan.cpp
  kernels/DeviceConvectionKernel.cu
  # GPU-DISC-001D closure: the production momentum convection contribution.
  kernels/DeviceMomentumConvectionPlan.cpp
  kernels/DeviceMomentumConvectionKernel.cu
  # GPU-DISC-001E: the shared boundary-condition layer.
  kernels/DeviceBoundaryConditions.cpp
  kernels/DeviceBoundaryConditionsKernel.cu
  # GPU-DISC-001F: the SIMPLE momentum assembly.
  kernels/DeviceMomentumAssemblyPlan.cpp
  kernels/DeviceMomentumAssemblyKernel.cu
  # GPU-DISC-001G: the momentum response coefficient.
  kernels/DeviceMomentumResponseKernel.cu
  # GPU-DISC-001H: the predicted face flux / Rhie-Chow.
  kernels/DeviceFaceFluxPlan.cpp
  kernels/DeviceFaceFluxKernel.cu
  # GPU-DISC-001I: the pressure-correction assembly.
  kernels/DevicePressureCorrectionPlan.cpp
  kernels/DevicePressureCorrectionKernel.cu
  # GPU-DISC-001J: the least-squares gradient and the cell-velocity correction.
  kernels/DeviceLeastSquaresGradientPlan.cpp
  kernels/DeviceLeastSquaresGradientKernel.cu
  kernels/DeviceVelocityCorrectionPlan.cpp
  kernels/DeviceVelocityCorrectionKernel.cu
  # GPU-DISC-001K: the face-flux correction.
  kernels/DeviceFaceFluxCorrectionKernel.cu
  kernels/DevicePersistentFieldsKernel.cu
  # GPU-PIPE-001: the device-side input checks and Jacobi diagonal the
  # GPU-resident pressure solve needs in place of their host counterparts.
  kernels/GpuResidentSolveKernel.cu
  # GPU-DISC-001M: the production GPU SIMPLE discretization entry point.
  kernels/GpuSimpleDiscretizationCuda.cpp
)

# GPU-DISC-001B requires BITWISE equality with the CPU gradient. The CPU library
# is built for baseline x86-64, which has no FMA instruction, so `a*b + c` is a
# rounded multiply then a rounded add. nvcc contracts that into a single FMA by
# default -- more accurate, and therefore a different number. Disable contraction
# for this translation unit only; every other kernel keeps the default, so no
# already-qualified kernel changes behaviour.
set_source_files_properties(
  kernels/DeviceGradientKernel.cu
  kernels/DeviceDiffusionKernel.cu
  kernels/DeviceConvectionKernel.cu
  kernels/DeviceMomentumConvectionKernel.cu
  kernels/DeviceBoundaryConditionsKernel.cu
  kernels/DeviceMomentumAssemblyKernel.cu
  kernels/DeviceMomentumResponseKernel.cu
  kernels/DeviceFaceFluxKernel.cu
  kernels/DevicePressureCorrectionKernel.cu
  kernels/DeviceLeastSquaresGradientKernel.cu
  kernels/DeviceVelocityCorrectionKernel.cu
  kernels/DeviceFaceFluxCorrectionKernel.cu
  kernels/DevicePersistentFieldsKernel.cu
  kernels/GpuResidentSolveKernel.cu
  PROPERTIES COMPILE_OPTIONS "-fmad=false"
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
