add_executable(CFDGpuTests
  # P17.7 -- HLLC CPU/GPU equivalence. The target is BITWISE, because host and device compile the
  # SAME CFD_HLLC_HOST_DEVICE source; the kernel is built with --fmad=false so contraction cannot be
  # the reason they differ. Includes the V2 control on device, so a GPU path cannot silently be a
  # different scheme.
  test_hllc_gpu_equivalence.cpp
  # P17.10 -- the DENSITY-BASED SOLVER, CPU against GPU. P17.7 above qualified the FLUX; this
  # one qualifies the residual assembly, the CFL reduction, the update and the validity
  # decisions. BITWISE throughout, with every negative control a DeviceControls flag rather
  # than a source edit, so a control run and the production run are the same binary.
  test_density_based_gpu_equivalence.cpp
  # P18.8 -- LINK TWO of the species equivalence chain: nvcc's DEVICE code against the SAME
  # shared header compiled for nvcc's host. Link one lives in tests/unit/species, where it
  # also runs in the CPU-only configuration; splitting them is what lets a failure name
  # which half broke instead of only that the two ends disagree.
  test_device_species_gpu_equivalence.cpp
  # P18.8 amendment A2 -- the PRE-FREEZE dry-run, and afterwards the standing non-vacuity
  # controls for A2's derived bounds. Includes the --fmad=true control translation unit, which
  # is what makes the relaxed bound provably unable to hide FMA contraction.
  test_device_species_a2_dryrun.cpp
  # P18.8 -- E11 (the device refuses what the host refuses, naming the same FIRST offender) and
  # the LIVE CONTROLS for E13. The controls are DISABLED_ so an ordinary ctest run never
  # performs a deliberate out-of-bounds access; the sanitizer harness invokes them explicitly.
  # A clean compute-sanitizer log is indistinguishable from a sanitizer that instrumented
  # nothing, so E13 is only ever reported next to a control the tool must flag.
  test_device_species_sanitizer_control.cpp
  test_cuda_spmv.cpp
  test_device_buffer.cpp
  test_gpu_residency_manager.cpp
  test_gpu_linear_solver.cpp
  test_boundary_encoding.cpp
  # P16-GPU-TURB C0: per-operation execution evidence + fallback control.
  test_turbulence_backend_trace.cpp
  # P16-GPU-TURB: device under-relaxation and imposed-row semantics, against
  # the CPU P14 transport path they have to reproduce.
  test_device_scalar_transport_semantics.cpp
  # P16-GPU-TURB: strain rate -> P_k -> mu_t, bitwise against the CPU path.
  test_device_turbulence_algebra.cpp
  # P16-GPU-TURB Stage 4: the k transport equation, CPU against GPU.
  test_device_k_transport.cpp
  # P16-GPU-TURB Stage 5: epsilon transport, including imposed wall cells.
  test_device_epsilon_transport.cpp
  # P16-GPU-TURB Stage 6: one complete correction vs KEpsilonModel::correct.
  test_device_kepsilon_correction.cpp
  # P16-GPU-TURB Stage 7D/7G/7H: the PRODUCTION KEpsilonModel path, CPU vs GPU.
  test_production_kepsilon_backend.cpp
  # P16-GPU-TURB Stage 7I step 2: the resident k-epsilon entry point.
  test_resident_kepsilon_entry.cpp
  # P16-GPU-TURB Stage 7I step 3: the compact CPU wall exchange.
  test_resident_wall_exchange.cpp
  # P16 BLOCKER / DEBT-GPU-001: was the blocker reproducer, now the GPU
  # BiCGSTAB restart-parity qualification gate and its non-vacuity control.
  test_krylov_blocker_reproducer.cpp
  # P16 resident turbulence: the device convergence reduction that replaced
  # four full-field downloads per correction.
  test_device_max_abs_difference.cpp
  # P16 Phase 1: mu_eff = mu + mu_t on device, and the resident handoff that
  # removed the last per-iteration viscosity round trip.
  test_device_effective_viscosity.cpp
  # P16 Phase 2: the sparse wall-face viscosity override in GPU momentum.
  test_device_wall_face_viscosity.cpp
  # P16 Step 4: turbulent k-epsilon through the real resident SIMPLE loop.
  test_resident_turbulent_simple.cpp
  # P16 Step 4: the production wall-function differential on the validated
  # Re_tau = 590 channel.
  test_resident_channel_production.cpp
)

target_link_libraries(CFDGpuTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# DistortedMesh.hpp: the non-orthogonal mesh the device diffusion correction is
# compared on, shared with tests/unit/discretization.
target_include_directories(CFDGpuTests
  PRIVATE "${CFDApp_SOURCE_DIR}/tests/unit/discretization")

gtest_discover_tests(CFDGpuTests PROPERTIES LABELS "numerical")

# P17.7: test_hllc_gpu_equivalence.cpp shares the GENERATED independent-reference header with the
# CPU HLLC suite rather than duplicating those 60-digit values.
target_include_directories(CFDGpuTests PRIVATE "${CMAKE_SOURCE_DIR}/tests/unit/compressible")

# P17.10: the comparison instrument (EquivalenceCompare.hpp) is shared with the pre-freeze
# dry-run that qualified it, rather than duplicated here.
target_include_directories(CFDGpuTests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests/integration/compressible")
