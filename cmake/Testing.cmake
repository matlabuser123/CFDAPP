# Helper for defining a CFDApp test as its own small executable registered
# with CTest. Centralized here so every test directory (tests/unit/core,
# tests/unit/mesh, tests/numerical/..., ...) wires a test the same way
# instead of repeating add_executable/target_link_libraries/add_test.
#
# Usage (from a tests/**/CMakeLists.txt):
#   cfdapp_add_test(CoreTypesTest TypesTest.cpp)
#
# CTest label convention (TODO.md P1 -- Quality Gate section 39): every
# gtest_discover_tests()/add_test() call in tests/**/CMakeLists.txt tags
# its tests with exactly ONE tier label -- "unit", "numerical", "solver",
# "integration", or "validation" -- via `PROPERTIES LABELS "<tier>"`, e.g.
# `ctest -L validation`. Deliberately one label, not a semicolon-separated
# list ("unit;numerical"): gtest_discover_tests re-serializes a list
# PROPERTIES value as bare space-separated words (`LABELS unit
# numerical`), which set_tests_properties' PROPERTIES then parses as
# prop/value *pairs* -- LABELS=unit, then a stray "numerical" property
# name with no value -- silently dropping the second label instead of
# registering it (confirmed via `ctest --print-labels`, CMake 3.22).
# Long-running validation cases stay excluded from a normal `ctest` run
# via their own DISABLED_ prefix (see tests/integration/cavity/
# test_cavity_ghia.cpp), which is what actually keeps CI fast -- these
# labels are for selecting a *tier* (e.g. every validation test) on
# demand, not for that slow/fast split.
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
