# P13-006 -- the other half of test_package_naming.cmake: an unsupported
# system must REFUSE, not invent a plausible name.
#
# A separate process because a CMake script cannot catch its own FATAL_ERROR.
# Registered with WILL_FAIL, so ctest treats the nonzero exit as the pass.
#
# Why this matters rather than being pedantic: "CFDApp-0.2.0-Haiku-x64.tar.gz"
# is a claim that CFDApp runs on Haiku. Nobody has built or run it there. The
# one rule this item is under is not to claim a platform that was not tested,
# and a name is a claim.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CFDAPP_SOURCE_DIR)
  message(FATAL_ERROR "CFDAPP_SOURCE_DIR must be set")
endif()

include(${CFDAPP_SOURCE_DIR}/cmake/PackageNaming.cmake)

cfdapp_package_naming(
  SYSTEM Haiku ARCH x86_64 VERSION 0.2.0 SUFFIX ""
  OUT_NAME name OUT_GENERATORS generators)

# Unreachable when the function behaves. If it is reached, the function
# invented a name for a platform nobody has tested -- say so loudly, and exit
# 0 so the WILL_FAIL test FAILS.
message(STATUS "cfdapp_package_naming did NOT refuse: name=${name} generators=${generators}")
