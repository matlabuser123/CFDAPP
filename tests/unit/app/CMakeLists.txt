add_executable(CFDAppLayerTests
  test_project_runner.cpp
  test_case_session.cpp
  test_visualization_snapshot.cpp
)

target_link_libraries(CFDAppLayerTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDAppLayerTests
  PROPERTIES
    LABELS "unit"
    WORKING_DIRECTORY ${CFDApp_SOURCE_DIR}
)
