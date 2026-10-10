# P4 -- Performance: standalone, explicitly-invoked benchmark tooling --
# deliberately NOT part of the `tests/` tree or the routine CTest suite
# (TODO.md P4 section 27: "Do not put fragile wall-clock limits into
# routine unit tests"). Each benchmark executable reuses the existing,
# unmodified production solver code directly.
add_subdirectory(cpu)

if(CFDAPP_ENABLE_CUDA)
  add_subdirectory(gpu)
endif()

# --- P13-007 -- a ctest smoke entry for the benchmark suite ----------------
# Registered HERE, not in tests/CMakeLists.txt, and that is the whole point:
# the root CMakeLists adds tests/ (line ~114) BEFORE benchmarks/ (line ~137),
# so an `if(TARGET cfd_benchmark_cpu)` guard written in tests/ is false and
# silently drops the test. It did: the regression that was supposed to cover
# this entry reported test counts identical to the previous item's
# (2048 / 2042 / 1982), which is how the omission was caught.
#
# The root CMakeLists already carries a comment about the same class of bug
# from P5 -- gtest_discover_tests() in a subdirectory added before
# enable_testing(), found because `ctest -N` did not list tests whose binary
# passed standalone. This is that bug's second appearance.
#
# What this is: a SMOKE entry. It runs the short CPU benchmark into a private
# directory and checks it produced the files the regression comparator reads.
# It asserts nothing about timing -- a timing assertion running alongside 2000
# other tests would measure the machine, not the code, which is exactly the
# effect that forced results/p13/benchmarks/acceptance_gate_A1.md.
add_test(
  NAME CFDAppBenchmarkSuiteSmoke
  COMMAND "${CMAKE_COMMAND}"
          -DBENCHMARK=$<TARGET_FILE:cfd_benchmark_cpu>
          -DOUT=${CMAKE_CURRENT_BINARY_DIR}/benchmark_smoke
          -P "${CFDApp_SOURCE_DIR}/tests/cmake/RunBenchmarkSmoke.cmake"
)
set_tests_properties(CFDAppBenchmarkSuiteSmoke PROPERTIES
  PASS_REGULAR_EXPRESSION "benchmark smoke: ok"
  LABELS "integration")
