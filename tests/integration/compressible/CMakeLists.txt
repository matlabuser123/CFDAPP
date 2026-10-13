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

# P17.6 -- the low-Mach FAMILY. A SEPARATE executable so the P3-PHYS-006 test above stays byte-for-
# byte unchanged, as the frozen G5 criterion requires. This one does what that one's own header says
# it does not: it solves the compressible system with CompressibleSIMPLE at four decreasing inlet
# Mach numbers and compares each against its own matched incompressible SIMPLE reference, so the
# convergence to the incompressible limit is measured rather than true by construction.
add_executable(CFDLowMachFamilyTests
  test_low_mach_family.cpp
)
target_link_libraries(CFDLowMachFamilyTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDLowMachFamilyTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

# P17.8 -- the density-based solver's canonical integration cases: a smooth advected pulse with an
# analytic reference and a refinement family (C4), an open-domain conservation balance against the
# integrated boundary flux (G2), and a low-Mach sanity run whose CONDITIONING is measured rather
# than claimed (section 27). A SEPARATE executable, so neither low-Mach test above is touched.
add_executable(CFDDensityBasedCanonicalTests
  test_density_based_canonical.cpp
)
target_link_libraries(CFDDensityBasedCanonicalTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
gtest_discover_tests(CFDDensityBasedCanonicalTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

# P17.9 -- shock validation. Sod, normal shock, contacts, supersonic advection, compression and
# expansion waves, measured against an INDEPENDENT 60-digit exact Riemann / Rankine-Hugoniot
# reference (results/p17/shock_validation/tools/riemann_reference.py). shock_reference_data.hpp
# beside it is GENERATED from that frozen script and must not be hand-edited.
add_executable(CFDShockValidationTests
  test_shock_validation.cpp
)
target_link_libraries(CFDShockValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
target_include_directories(CFDShockValidationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(CFDShockValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

# P17.10 -- THE DRY-RUN, run before the acceptance gate is frozen (CLAUDE.md 5.2). No CUDA: it
# measures the ascending-face-list invariant that every bitwise claim rests on, and exercises the
# comparison instrument in EquivalenceCompare.hpp for BOTH behaviours -- including every way a
# comparison can silently not happen and still look like a clean pass.
add_executable(CFDP17_10DryRunTests
  test_p17_10_dryrun.cpp
)
target_link_libraries(CFDP17_10DryRunTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)
target_include_directories(CFDP17_10DryRunTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(CFDP17_10DryRunTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
