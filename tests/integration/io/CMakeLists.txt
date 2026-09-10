# P1 -- Result Export integration test: solve -> export all formats ->
# cross-check every value against SIMPLEResult, plus repeated-export
# determinism.
add_executable(CFDResultExportIntegrationTests
  test_result_export.cpp
  test_paraview_smoke.cpp
  ${CFDApp_SOURCE_DIR}/tests/support/CaseFixtureCopy.cpp
)

target_link_libraries(CFDResultExportIntegrationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
    # Test-only, same rationale as tests/unit/io/test_json_writer.cpp.
    nlohmann_json::nlohmann_json
)

# P7-TEST-001: CaseFixtureCopy.hpp -- see its own header comment.
target_include_directories(CFDResultExportIntegrationTests PRIVATE "${CFDApp_SOURCE_DIR}/tests/support")

gtest_discover_tests(CFDResultExportIntegrationTests
  PROPERTIES
    LABELS "integration"
    WORKING_DIRECTORY ${CFDApp_SOURCE_DIR}
)
