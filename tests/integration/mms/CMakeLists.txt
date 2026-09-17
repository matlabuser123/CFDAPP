# P12-NUM-006 -- system-level method-of-manufactured-solutions verification:
# scalar advection-diffusion (ThermalSolver), momentum only, pressure-
# gradient balance, continuity / face flux, full SIMPLE and
# CompressibleSIMPLE, with analytical forcing through the production
# (generic) volumetric source terms.
# Enabled in the default suite: every study except
# SIMPLEMMS.DISABLED_FineGridStudy (16/32/64, ~25 min Debug -- run with
# --gtest_also_run_disabled_tests). Reports: results/validation/mms/.
# P12-MESH-005 (test_mms_3d.cpp): 3D operator MMS; the 8^3..64^3 gate study
# MMS3DTest.DISABLED_OperatorRefinementStudy is run explicitly in Release and
# writes results/p12-mesh-005/data/.
# P12-MESH-006 (test_mms_simple3d.cpp): 3D production SIMPLE MMS; the gate
# study MMSSimple3DTest.DISABLED_RefinementStudy (8^3..32^3) is run explicitly
# in Release and writes results/p12-mesh-006/data/.
add_executable(CFDMMSValidationTests
  MMSCases.cpp
  test_mms_scalar.cpp
  test_mms_momentum.cpp
  test_mms_simple.cpp
  test_mms_compressible.cpp
  test_mms_3d.cpp
  test_mms_simple3d.cpp
)

target_link_libraries(CFDMMSValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# ManufacturedFields.hpp / DistortedMesh.hpp: the shared MMS utilities of
# the operator-level tests, reused (not copied).
target_include_directories(CFDMMSValidationTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CFDApp_SOURCE_DIR}/tests/unit/discretization
)

gtest_discover_tests(CFDMMSValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
