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
  # P17.7 -- the HLLC flux on device. THE ONLY kernel in this library that contains no physics:
  # every formula lives in include/cfd/compressible/HllcFlux.hpp, marked CFD_HLLC_HOST_DEVICE, and
  # gcc and nvcc compile that same source. See the --fmad=false property set below.
  kernels/DeviceHllcFluxKernel.cu
  # P17.10 -- the density-based solver on device. Like the HLLC kernel above it restates no
  # Riemann numerics; the only restatements in the phase are the thermodynamic closed forms in
  # include/cfd/gpu/DeviceThermodynamics.hpp, each proved against production by a direct
  # cross-comparison test. --fmad=false below, for the same reason and by the same convention.
  kernels/DeviceDensityBasedKernel.cu
  kernels/DevicePressureCorrectionPlan.cpp
  kernels/DevicePressureCorrectionKernel.cu
  # GPU-DISC-001J: the least-squares gradient and the cell-velocity correction.
  kernels/DeviceLeastSquaresGradientPlan.cpp
  kernels/DeviceLeastSquaresGradientKernel.cu
  kernels/DeviceVelocityCorrectionPlan.cpp
  kernels/DeviceVelocityCorrectionKernel.cu
  # GPU-DISC-001K: the face-flux correction.
  kernels/DeviceFaceFluxCorrectionKernel.cu
  # P14: the generic scalar transport composition (source, transient, merge).
  kernels/DeviceScalarTransportKernel.cu
  # P16-GPU-TURB: strain rate, production and eddy viscosity on device.
  kernels/DeviceTurbulenceKernel.cu
  kernels/DeviceSpeciesKernel.cu
  # P18.8 amendment A2 -- the --fmad=true NON-VACUITY CONTROL for the kernel above. Same shared
  # arithmetic header, same kernel bodies; the compile flag below is the only difference, so a
  # measured difference attributes to FMA contraction and to nothing else. Never a solver path:
  # the only caller is the equivalence suite. See the header for the full rationale.
  kernels/DeviceSpeciesFmadControl.cu
  # P16-GPU-TURB Stage 6: one k-epsilon correction, composed in the
  # production order. Orchestration only -- every kernel it uses exists.
  kernels/DeviceKEpsilonCuda.cpp
  # P16-GPU-TURB Stage 7D: the production seam. CUDA-free facade in
  # include/cfd/gpu/GpuKEpsilonCorrection.hpp; the CPU-only stub is in
  # src/gpu/ under if(NOT CFDAPP_ENABLE_CUDA).
  kernels/GpuKEpsilonCorrectionCuda.cpp
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
  kernels/DeviceScalarTransportKernel.cu
  kernels/DeviceTurbulenceKernel.cu
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

# P17.7 -- FMA contraction OFF for the HLLC kernel.
#
# nvcc fuses a*b+c into a single FMA by default; the host x86-64 build does not (the SSE2 baseline
# has no FMA instruction). Since HllcFlux.hpp is ONE source compiled for both, that difference -- and
# nothing else -- would break the bitwise CPU/GPU equality that P17_GATES.md G8 calls "the correct
# requirement where the operation order is identical".
#
# THIS IS THE PROJECT'S ESTABLISHED CONVENTION, NOT A NEW CONCESSION: the set_source_files_properties
# block above already applies -fmad=false to the twenty-odd existing kernels for the same reason. A
# SEPARATE property is used here only so that results/p17/hllc/tools/fma_experiment.sh can flip this
# one translation unit without disturbing the rest of the library.
#
# What the flag is worth, MEASURED (logs/05_fma_experiment.log, criterion G3):
#   --fmad=false   17 of 17 rows BITWISE, max |flux diff| exactly 0
#   --fmad=true     8 of 17 rows bitwise, max |flux diff| 1.52587890625e-04 absolute
#                   (on an energy flux of 8.19e10, i.e. 1.9e-15 relative)
#   the WAVE SPEEDS were unaffected either way -- only the flux and the star internal energy moved
#   branch selection never differed, so contraction was never close to changing the physics
# Restored and re-verified bitwise afterwards, with the restore proven by SHA-256.
set_source_files_properties(kernels/DeviceHllcFluxKernel.cu PROPERTIES
  COMPILE_OPTIONS "--fmad=false")

# P17.10 -- the same flag, for the same reason, on the density-based kernels. It is NOT introduced
# here to force this phase's agreement: it predates P17.10 by two phases and already applies to
# the twenty-odd kernels in the block above. A SEPARATE property so the V7 control can flip this
# one translation unit and REPORT what contraction costs, exactly as P17.7 did (17/17 bitwise
# with it, 8/17 without).
set_source_files_properties(kernels/DeviceDensityBasedKernel.cu PROPERTIES
  COMPILE_OPTIONS "--fmad=false")

# P18.8 -- the same flag, for the same reason, on the species/chemistry kernel. It is NOT
# introduced here to force this phase's agreement: it predates P18.8 by three phases and already
# applies to the twenty-odd kernels in the set_source_files_properties list above, which also
# covers this file. A SEPARATE property so a control can flip this ONE translation unit and
# REPORT what contraction costs, exactly as P17.7 and P17.10 did.
#
# G9 names --fmad=false as condition (4) of its bitwise derivation. The species thermochemistry
# is CELL-LOCAL, so conditions (2) accumulation order, (3) uploaded geometry and (5)
# order-independent reductions are satisfied vacuously -- which leaves (1) one shared source and
# (4) this flag as the only two things the bitwise claim actually rests on here.
set_source_files_properties(kernels/DeviceSpeciesKernel.cu PROPERTIES
  COMPILE_OPTIONS "--fmad=false")

# P18.8 amendment A2: the control translation unit, compiled with contraction ENABLED.
#
# A2 replaces the unachievable BITWISE criterion on the four transcendental-dependent quantities
# (k, q, omega_i, S_E) with a derived ULP bound, and keeps BITWISE for everything else. A bound is
# only worth stating if something can violate it, and the thing it must not hide is contraction.
# This TU is how that is MEASURED rather than asserted.
#
# --fmad=true is also nvcc's default, so the property is redundant today. It is set explicitly
# anyway: a control that silently stopped contracting because a default changed would report
# exactly what success looks like. fmadControlIsReallyContracting() checks the same thing at
# runtime, on the device, with a dyadic discriminator.
set_source_files_properties(kernels/DeviceSpeciesFmadControl.cu PROPERTIES
  COMPILE_OPTIONS "--fmad=true")
#
# WHAT CONTRACTION MEASURABLY COSTS HERE, reported the way P17.7 reports its own (line 133):
#
#   --fmad=false   M_mix 0/4096 differ, R_mix 0/4096, cp_mix 0/4096, C_i 0/12288,
#                  omega_i 0/12288, S_E 0/4096   -- every one BITWISE
#   --fmad=true    M_mix 0/4096 (divide-then-add: FMA cannot reach it),
#                  R_mix 662/4096 differ worst 2 ULP, cp_mix 543/4096 worst 1 ULP,
#                  C_i 0/12288 (no a*b+c site), omega_i 0/12288, S_E 1/4096 worst 1 ULP
#
# AND THE NUMBER THAT DECIDED WHERE G9 CONDITION (4) IS POLICED. Measured in the units of A2's
# bound on q -- |dq| / (eps * sum of term magnitudes):
#
#   contraction                              0.843356
#   the libm difference the bound must ADMIT  3.13865
#
# Contraction is the SMALLER effect, so no threshold on q that lets a correct implementation
# pass can reject contraction. Condition (4) therefore rests on the BITWISE mixture-property
# criterion above, which is compiled from the SAME translation unit carrying the SAME single
# --fmad=false property -- so the flag cannot be lost for the rate path alone without that
# zero-tolerance criterion failing. Derived in results/p18/cpu_gpu_equivalence/
# acceptance_gate_A2.md section 4; measured in logs/06_A2_dryrun_PRE_FREEZE.log.

target_link_libraries(cfdcuda
  PUBLIC
    CUDA::cudart
)
