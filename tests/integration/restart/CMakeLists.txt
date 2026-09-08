# P2 -- Restart capability, Restart-C/D: RestartWriter/RestartReader
# round-trip and failure-path integration tests.
add_executable(CFDRestartIntegrationTests
  test_restart_io.cpp
  test_restart_resume.cpp
)

target_link_libraries(CFDRestartIntegrationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

gtest_discover_tests(CFDRestartIntegrationTests PROPERTIES LABELS "integration")
