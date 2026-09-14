# P12-COMP-002: CompressibleSIMPLE and its two new building blocks
# (assembleRelaxedCompressibleMomentumComponent,
# assembleCompressiblePressureCorrection) -- a genuinely coupled
# compressible pressure-velocity solver, dedicated and separate from
# cfd::pressure_velocity::SIMPLE (untouched, tested under tests/solver/simple).
add_executable(CFDCompressibleSimpleTests
  test_compressible_relaxed_momentum.cpp
  test_compressible_pressure_correction.cpp
  test_compressible_simple.cpp
  test_compressible_non_orthogonal.cpp
  test_compressible_robustness.cpp
)

target_link_libraries(CFDCompressibleSimpleTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-NUM-003: reuses tests/unit/discretization/DistortedMesh.hpp.
target_include_directories(CFDCompressibleSimpleTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../unit/discretization
)

gtest_discover_tests(CFDCompressibleSimpleTests PROPERTIES LABELS "solver")
