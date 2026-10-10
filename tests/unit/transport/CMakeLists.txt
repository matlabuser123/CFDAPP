add_executable(CFDTransportTests
  test_scalar_transport.cpp
  test_transport_characterization.cpp
)

target_link_libraries(CFDTransportTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# Reuses the shared distorted-mesh helper rather than duplicating it.
target_include_directories(CFDTransportTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../discretization
)

gtest_discover_tests(CFDTransportTests PROPERTIES LABELS "unit")
