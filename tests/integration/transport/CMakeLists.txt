add_executable(CFDTransportVerificationTests
  test_transport_verification.cpp
)

target_link_libraries(CFDTransportVerificationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDTransportVerificationTests PROPERTIES LABELS "validation")
