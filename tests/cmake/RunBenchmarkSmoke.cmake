# P13-007 -- prove the benchmark suite still runs and still writes what the
# regression comparator needs. Not a performance assertion: see the comment at
# the add_test() site for why timing inside ctest would measure the machine.
cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

execute_process(
  COMMAND "${BENCHMARK}" --output-dir "${OUT}"
  RESULT_VARIABLE status
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "benchmark exited ${status}\n${out}\n${err}")
endif()

# The files the comparator reads. A benchmark that runs but stops writing these
# would leave the regression check comparing nothing -- which the comparator
# treats as a failure, but only if it is ever run against a real directory.
file(GLOB produced "${OUT}/benchmark_*.json")
list(LENGTH produced count)
if(count LESS 3)
  message(FATAL_ERROR "expected at least 3 benchmark_*.json, found ${count} in ${OUT}")
endif()

if(NOT EXISTS "${OUT}/environment.json")
  message(FATAL_ERROR "no environment.json written to ${OUT}")
endif()

message(STATUS "benchmark smoke: ok -- ${count} result files in ${OUT}")
