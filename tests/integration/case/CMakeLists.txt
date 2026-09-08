# P1 -- Case System integration test: load -> validate -> build ->
# solve -> assert convergence, against both a fast fixture and (DISABLED_)
# the real production case.
add_executable(CFDCaseIntegrationTests
  test_case_lid_driven_cavity.cpp
)

target_link_libraries(CFDCaseIntegrationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDCaseIntegrationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
