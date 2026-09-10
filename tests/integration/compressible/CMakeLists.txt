# P3-PHYS-006: low-Mach compressible-foundation regression -- a genuine
# incompressible SIMPLE-solved channel flow, reinterpreted through the
# compressible foundation (IdealGasEOS/ThermodynamicProperties,
# calculateCompressibleMassFlux, evaluateCompressibleContinuity) to
# demonstrate EOS consistency, a small Mach number, small density
# variation, and close agreement with the incompressible mass flux in
# that limit -- see this task's own disclosed scope note in TODO.md for
# why this is a post-hoc consistency demonstration built on the existing
# incompressible solver, not a separately-iterated compressible pressure-
# correction solve.
add_executable(CFDLowMachRegressionTests
  test_low_mach_regression.cpp
)
target_link_libraries(CFDLowMachRegressionTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDLowMachRegressionTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
