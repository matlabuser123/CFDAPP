# P2-TURB-007 -- Turbulence Benchmark Validation: turbulent plane
# channel flow vs the Re_tau=180 literature reference. Fine-grid/grid-
# refinement-trend cases are marked DISABLED_ in the test bodies
# (runtime: up to ~1-2 minutes) -- run them explicitly with
# --gtest_also_run_disabled_tests when regenerating full validation
# evidence, per the file header comment in test_channel_flow_validation.cpp.
add_executable(CFDTurbulenceChannelValidationTests
  # P16: the wall-function benchmark family (its own meshes; the pre-P16
  # 64x16 case above stays untouched as the preserved baseline).
  test_wall_function_channel.cpp
  ChannelFlowValidationUtils.cpp
  test_channel_flow_validation.cpp
)

target_link_libraries(CFDTurbulenceChannelValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

target_include_directories(CFDTurbulenceChannelValidationTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# P16 Step 4 gate 2 -- the GPU arm of the frozen wall-function benchmark.
#
# This target builds in BOTH configurations, but turbulenceGpuPathQualified()
# is defined only in cuda/kernels/ (compiled only when CFDAPP_ENABLE_CUDA=ON).
# Without this definition the GPU benchmark would be a CPU-only link failure --
# the blind spot that tests/unit/gpu avoids by not existing at all when CUDA
# is off. The GPU test still checks cfd::gpu::cudaAvailable() at runtime and
# skips when the build has CUDA but the machine has no device.
if(CFDAPP_ENABLE_CUDA)
  target_compile_definitions(CFDTurbulenceChannelValidationTests
    PRIVATE CFDAPP_TEST_WITH_CUDA=1)
endif()

gtest_discover_tests(CFDTurbulenceChannelValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
