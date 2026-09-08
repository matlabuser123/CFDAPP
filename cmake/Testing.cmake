# Helper for defining a CFDApp test as its own small executable registered
# with CTest. Centralized here so every test directory (tests/unit/core,
# tests/unit/mesh, tests/numerical/..., ...) wires a test the same way
# instead of repeating add_executable/target_link_libraries/add_test.
#
# Usage (from a tests/**/CMakeLists.txt):
#   cfdapp_add_test(CoreTypesTest TypesTest.cpp)
function(cfdapp_add_test test_name source_file)
  add_executable(${test_name} ${source_file})

  target_link_libraries(${test_name}
    PRIVATE
      cfdapp::core
      cfdapp::compiler_warnings
  )

  target_include_directories(${test_name}
    PRIVATE
      "${CMAKE_SOURCE_DIR}/tests/support"
  )

  add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()
