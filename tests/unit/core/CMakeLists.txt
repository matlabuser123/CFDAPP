add_executable(CFDCoreTests
  test_types.cpp
  test_constants.cpp
  test_exception.cpp
  test_logger.cpp
  test_timer.cpp
)

target_link_libraries(CFDCoreTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDCoreTests PROPERTIES LABELS "unit")
