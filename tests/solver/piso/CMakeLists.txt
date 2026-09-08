add_executable(CFDPisoTests
  test_transient_momentum.cpp
  test_pressure_correction.cpp
)

target_link_libraries(CFDPisoTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDPisoTests PROPERTIES LABELS "solver")
