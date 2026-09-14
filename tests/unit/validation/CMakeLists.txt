# P12-NUM-005: three-grid convergence analysis (GridConvergence.hpp) and
# the automated study / reports (GridConvergenceStudy.hpp). Fast, pure
# math and bookkeeping -- always part of the default suite.
# P12-NUM-006: the system-level MMS building blocks (analytical fields and
# forcing, generic volumetric sources, error norms / pressure gauge, MMS
# report) -- test_manufactured_solutions.cpp, also fast.
# P12-NUM-007: sample-point norms, the production-validation record /
# report and the SIMPLE linear-iteration counters --
# test_production_validation.cpp, fast.
add_executable(CFDValidationUnitTests
  test_grid_convergence.cpp
  test_grid_convergence_study.cpp
  test_manufactured_solutions.cpp
  test_production_validation.cpp
)

target_link_libraries(CFDValidationUnitTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
    # Test-only: parses the JSON report back to validate it (same
    # convention as tests/unit/io's JSONWriter tests).
    nlohmann_json::nlohmann_json
)

# ManufacturedFields.hpp / DistortedMesh.hpp -- the shared MMS test
# utilities live with the operator-level MMS tests; reused, not copied.
target_include_directories(CFDValidationUnitTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../discretization
)

gtest_discover_tests(CFDValidationUnitTests PROPERTIES LABELS "unit")
