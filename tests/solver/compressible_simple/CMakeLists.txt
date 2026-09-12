# P12-COMP-002: CompressibleSIMPLE and its two new building blocks
# (assembleRelaxedCompressibleMomentumComponent,
# assembleCompressiblePressureCorrection) -- a genuinely coupled
# compressible pressure-velocity solver, dedicated and separate from
# cfd::pressure_velocity::SIMPLE (untouched, tested under tests/solver/simple).
add_executable(CFDCompressibleSimpleTests
  test_compressible_relaxed_momentum.cpp
  test_compressible_pressure_correction.cpp
  test_compressible_simple.cpp
)

target_link_libraries(CFDCompressibleSimpleTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDCompressibleSimpleTests PROPERTIES LABELS "solver")
