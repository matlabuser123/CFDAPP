add_executable(CFDPisoTests
  test_transient_momentum.cpp
  test_pressure_correction.cpp
  test_correction_application.cpp
  test_second_pressure_correction.cpp
  test_final_correction.cpp
  test_piso.cpp
  test_transient_integration.cpp
  test_piso_turbulence.cpp
  # P12-MESH-007: ALE PISO on prescribed moving meshes.
  test_ale_piso.cpp
)

target_link_libraries(CFDPisoTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-MESH-007: tests/support/MeshMotionCases.hpp (the gate configurations),
# and -- for the GCL-inconsistent negative controls of acceptance_gate.md
# G5.4 (Amendment A2) only -- the library-internal PISO step
# (src/pressure_velocity/PisoStep.hpp), which is not a public header.
target_include_directories(CFDPisoTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../support
    ${PROJECT_SOURCE_DIR}/src/pressure_velocity
)

gtest_discover_tests(CFDPisoTests PROPERTIES LABELS "solver")
