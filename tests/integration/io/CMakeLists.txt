# P1 -- Result Export integration test: solve -> export all formats ->
# cross-check every value against SIMPLEResult, plus repeated-export
# determinism.
add_executable(CFDResultExportIntegrationTests
  test_result_export.cpp
)

target_link_libraries(CFDResultExportIntegrationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
    # Test-only, same rationale as tests/unit/io/test_json_writer.cpp.
    nlohmann_json::nlohmann_json
)

gtest_discover_tests(CFDResultExportIntegrationTests)
