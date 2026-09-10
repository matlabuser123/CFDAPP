# P2-THERMAL-004: heated-cavity (pure conduction) validation against the
# closed-form analytical profile T(x) = Th + (Tc-Th)*x/L. All three grids
# run in the default suite (unlike the momentum lid-driven-cavity's own
# 40x40/80x80, which are DISABLED_ for multi-minute runtime) -- the
# thermal solve here is a linear steady problem on a quiescent flow field
# and its 80x80 grid runs in well under a minute, so there is no runtime
# reason to disable it by default.
add_executable(CFDHeatedCavityValidationTests
  test_heated_cavity_validation.cpp
)

target_link_libraries(CFDHeatedCavityValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDHeatedCavityValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

# P2-THERMAL-005: conjugate heat-transfer foundation -- two-material
# composite-wall validation against the closed-form series-resistance
# solution. Fast (sub-second per grid, this is a small linear conduction
# problem), so all three grids run in the default suite.
add_executable(CFDConjugateHeatTransferValidationTests
  test_two_material_conduction.cpp
)

target_link_libraries(CFDConjugateHeatTransferValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDConjugateHeatTransferValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

# P3-PHYS-001: Boussinesq buoyancy coupling regression -- composes SIMPLE
# (temperature/buoyancy) and ThermalSolver in a small outer Picard loop
# this test file owns directly (see its own header comment). Not a
# benchmark-quality natural-convection validation.
add_executable(CFDBoussinesqCouplingTests
  test_boussinesq_coupling.cpp
)

target_link_libraries(CFDBoussinesqCouplingTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDBoussinesqCouplingTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)

# P3-PHYS-002: differentially-heated-cavity natural-convection validation
# vs. the de Vahl Davis (1983) benchmark. Grid20x20/grid-refinement cases
# are marked DISABLED_ in the test bodies (runtime: minutes, diagnosed
# solver-robustness sensitivity at this benchmark's required source
# magnitude -- see test_natural_convection_validation.cpp's own header
# comment) -- run explicitly with --gtest_also_run_disabled_tests when
# regenerating full validation evidence.
add_executable(CFDNaturalConvectionValidationTests
  NaturalConvectionValidationUtils.cpp
  test_natural_convection_validation.cpp
)

target_link_libraries(CFDNaturalConvectionValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDNaturalConvectionValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
