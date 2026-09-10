add_executable(CFDAppLayerTests
  test_project_runner.cpp
  test_case_session.cpp
  test_visualization_snapshot.cpp
  ${CFDApp_SOURCE_DIR}/tests/support/CaseFixtureCopy.cpp
)

target_link_libraries(CFDAppLayerTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P7-TEST-001: CaseFixtureCopy.hpp -- see its own header comment.
target_include_directories(CFDAppLayerTests PRIVATE "${CFDApp_SOURCE_DIR}/tests/support")

gtest_discover_tests(CFDAppLayerTests
  PROPERTIES
    LABELS "unit"
    WORKING_DIRECTORY ${CFDApp_SOURCE_DIR}
)
