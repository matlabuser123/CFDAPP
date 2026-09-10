# P1 -- Case System integration test: load -> validate -> build ->
# solve -> assert convergence, against both a fast fixture and (DISABLED_)
# the real production case.
add_executable(CFDCaseIntegrationTests
  test_case_lid_driven_cavity.cpp
  test_species_production_case.cpp
  test_multiphase_production_case.cpp
  test_compressible_production_case.cpp
  ${CFDApp_SOURCE_DIR}/tests/support/CaseFixtureCopy.cpp
)

target_link_libraries(CFDCaseIntegrationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P7-TEST-001: CaseFixtureCopy.hpp -- see its own header comment.
target_include_directories(CFDCaseIntegrationTests PRIVATE "${CFDApp_SOURCE_DIR}/tests/support")

gtest_discover_tests(CFDCaseIntegrationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
