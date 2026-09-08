add_executable(CFDIoTests
  test_case_reader.cpp
  test_case_validation.cpp
  test_case_builder.cpp
  test_csv_writer.cpp
  test_json_writer.cpp
  test_vtk_writer.cpp
)

target_link_libraries(CFDIoTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
    # Test-only: reads JSONWriter's output back to validate it, the same
    # way test_json_writer.cpp's own header comment explains -- never a
    # dependency of any public cfd/io header.
    nlohmann_json::nlohmann_json
)

target_include_directories(CFDIoTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(CFDIoTests PROPERTIES LABELS "unit")
