# P13-006 -- cfdapp_package_naming(), driven with `cmake -P`.
#
# The point of this file: the Windows artifact name is a contract that
# `.github/workflows/release.yml` globs (`build/CFDApp-*-Windows-x64.zip`,
# `...-Windows-x64.exe`) and that was verified on real Windows hardware for
# 0.1.0 and v0.2.0 -- but no Windows machine is available to this repository's
# routine testing. Because the naming decision is a pure function of the system
# name, it can be asserted for Windows from any host. That is the whole reason
# it is a function rather than four set() calls.
#
# Run standalone:  cmake -DCFDAPP_SOURCE_DIR=<repo> -P tests/cmake/test_package_naming.cmake

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CFDAPP_SOURCE_DIR)
  message(FATAL_ERROR "CFDAPP_SOURCE_DIR must be set")
endif()

include(${CFDAPP_SOURCE_DIR}/cmake/PackageNaming.cmake)

set(failures 0)

function(expect_naming label system arch version suffix expected_name expected_generators)
  cfdapp_package_naming(
    SYSTEM "${system}" ARCH "${arch}" VERSION "${version}" SUFFIX "${suffix}"
    OUT_NAME actual_name OUT_GENERATORS actual_generators)
  if(NOT actual_name STREQUAL expected_name)
    message(SEND_ERROR "${label}: name\n  expected: ${expected_name}\n  actual:   ${actual_name}")
  endif()
  if(NOT actual_generators STREQUAL expected_generators)
    message(SEND_ERROR
      "${label}: generators\n  expected: ${expected_generators}\n  actual:   ${actual_generators}")
  endif()
  message(STATUS "  ${label}: ${actual_name}  [${actual_generators}]")
endfunction()

message(STATUS "-- the Windows release contract (release.yml globs this name) --")
# A tagged release configures with -DCFDAPP_VERSION_SUFFIX="" (release.yml), so
# the release name carries no suffix. This is the exact string that was built,
# packaged and smoke-tested on real Windows hardware for v0.2.0.
expect_naming("windows release" Windows AMD64 0.2.0 "" "CFDApp-0.2.0-Windows-x64" "ZIP;NSIS")
expect_naming("windows x86_64 spelling" Windows x86_64 0.2.0 "" "CFDApp-0.2.0-Windows-x64" "ZIP;NSIS")

message(STATUS "-- a development build must not be named like a release --")
expect_naming("windows dev" Windows AMD64 0.2.0 dev "CFDApp-0.2.0-dev-Windows-x64" "ZIP;NSIS")
expect_naming("linux dev" Linux x86_64 0.2.0 dev "CFDApp-0.2.0-dev-Linux-x64" "TGZ;STGZ")

message(STATUS "-- the platform in the name is the platform built for --")
expect_naming("linux release" Linux x86_64 0.2.0 "" "CFDApp-0.2.0-Linux-x64" "TGZ;STGZ")
expect_naming("linux arm64" Linux aarch64 0.2.0 "" "CFDApp-0.2.0-Linux-arm64" "TGZ;STGZ")
expect_naming("macos arm64" Darwin arm64 0.2.0 "" "CFDApp-0.2.0-macOS-arm64" "TGZ")

message(STATUS "-- NSIS is requested only where it can exist --")
foreach(system Linux Darwin)
  cfdapp_package_naming(SYSTEM ${system} ARCH x86_64 VERSION 0.2.0 SUFFIX ""
                        OUT_NAME probe_name OUT_GENERATORS probe_generators)
  if(probe_generators MATCHES "NSIS")
    message(SEND_ERROR "${system} requests NSIS: ${probe_generators}")
  endif()
endforeach()
cfdapp_package_naming(SYSTEM Windows ARCH AMD64 VERSION 0.2.0 SUFFIX ""
                      OUT_NAME probe_name OUT_GENERATORS probe_generators)
if(NOT probe_generators MATCHES "NSIS")
  message(SEND_ERROR "Windows no longer requests NSIS: ${probe_generators}")
endif()
message(STATUS "  NSIS on Windows only: ok")

message(STATUS "-- an output variable named like a CMake false constant --")
# `n`, `no`, `off` and `ignore` are all false constants to if(<variable>), and
# an argument check written as `if(NOT PN_OUT_NAME)` rejected them. A caller
# should not have to know that, so it is asserted here.
foreach(awkward n no off ignore)
  cfdapp_package_naming(SYSTEM Linux ARCH x86_64 VERSION 0.2.0 SUFFIX ""
                        OUT_NAME ${awkward} OUT_GENERATORS ${awkward}_gen)
  if(NOT ${awkward} STREQUAL "CFDApp-0.2.0-Linux-x64")
    message(SEND_ERROR "output variable named '${awkward}' broke the call: ${${awkward}}")
  endif()
endforeach()
message(STATUS "  n / no / off / ignore as output variable names: ok")

# Deliberately NOT tested by asserting a name: an unsupported system must
# FATAL_ERROR, and a script cannot catch its own FATAL_ERROR. The refusal is
# checked by tests/cmake/test_package_naming_refuses.cmake, which is run as a
# separate WILL_FAIL process.
message(STATUS "cfdapp_package_naming: all expectations met")
