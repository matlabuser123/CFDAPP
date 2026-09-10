# P2-TURB-007 -- Turbulence Benchmark Validation: turbulent plane
# channel flow vs the Re_tau=180 literature reference. Fine-grid/grid-
# refinement-trend cases are marked DISABLED_ in the test bodies
# (runtime: up to ~1-2 minutes) -- run them explicitly with
# --gtest_also_run_disabled_tests when regenerating full validation
# evidence, per the file header comment in test_channel_flow_validation.cpp.
add_executable(CFDTurbulenceChannelValidationTests
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

gtest_discover_tests(CFDTurbulenceChannelValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
