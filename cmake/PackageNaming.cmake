# P13-006 -- the package's name and generator list, as a pure function.
#
# Why a function rather than four set() calls in Packaging.cmake: the Windows
# artifact name is a CONTRACT. `.github/workflows/release.yml` globs
# `build/CFDApp-*-Windows-x64.zip` and `...-Windows-x64.exe`, and the Windows
# packaging was verified on real hardware for 0.1.0 and v0.2.0. That contract
# cannot be exercised on the Linux development host -- but a function taking
# the system name as an ARGUMENT can be, with `cmake -P`, for Windows, Linux
# and Darwin alike. See tests/cmake/test_package_naming.cmake.
#
# What this replaces, measured rather than argued
# (results/p13/packaging/audit.md):
#
#   * `CPACK_PACKAGE_FILE_NAME "CFDApp-${VERSION}-Windows-x64"` was hardcoded,
#     so a Linux build produced CFDApp-0.2.0-Windows-x64.zip containing
#     "ELF 64-bit LSB pie executable ... for GNU/Linux". A package name that
#     misstates its own platform defeats the one thing a package name is for.
#   * `CPACK_GENERATOR "ZIP;NSIS"` was unconditional, so `cpack` exited 1 on
#     any host without makensis -- AFTER the ZIP generator had already written
#     an artifact. A script checking the status aborts holding a finished
#     package; one that does not ships the mislabelled zip.

include_guard(GLOBAL)

# cfdapp_package_naming(
#   SYSTEM     <CMAKE_SYSTEM_NAME>        e.g. Windows / Linux / Darwin
#   ARCH       <processor>                e.g. AMD64 / x86_64 / arm64
#   VERSION    <major.minor.patch>
#   [SUFFIX    <version suffix>]          e.g. dev, rc1; empty for a release
#   OUT_NAME       <variable>
#   OUT_GENERATORS <variable>
# )
#
# Refuses an unrecognised system with FATAL_ERROR rather than inventing a
# plausible-looking name. A package called "CFDApp-0.2.0-Haiku-x64" that
# nobody has ever built or run would be a claim of support, and the one rule
# this item is under is not to claim a platform that was not tested.
function(cfdapp_package_naming)
  set(options "")
  set(oneValueArgs SYSTEM ARCH VERSION SUFFIX OUT_NAME OUT_GENERATORS)
  cmake_parse_arguments(PN "${options}" "${oneValueArgs}" "" ${ARGN})

  # DEFINED + STREQUAL "" rather than `if(NOT PN_${required})`. The plain form
  # is a trap: if() evaluates a variable's VALUE against CMake's false
  # constants, and those include "N", "NO", "OFF" and "IGNORE" -- so passing
  # `OUT_NAME n` made the argument check reject a perfectly valid call because
  # the output variable happened to be named `n`. Found by the test below,
  # which is what a test is for.
  foreach(required SYSTEM ARCH VERSION OUT_NAME OUT_GENERATORS)
    if(NOT DEFINED PN_${required} OR PN_${required} STREQUAL "")
      message(FATAL_ERROR "cfdapp_package_naming: ${required} is required")
    endif()
  endforeach()

  # The version the package advertises is the version the binary reports.
  # cfd::core::versionString() is "<major>.<minor>.<patch>[-<suffix>]"
  # (src/core/Version.cpp), and before this the packaging dropped the suffix --
  # so a development build was named EXACTLY like the released 0.2.0 while
  # `cfdapp --version` inside it said 0.2.0-dev. A tagged release configures
  # with -DCFDAPP_VERSION_SUFFIX="" (release.yml), so the release name is
  # unchanged by this.
  set(version "${PN_VERSION}")
  if(PN_SUFFIX)
    set(version "${version}-${PN_SUFFIX}")
  endif()

  # One platform tag per system, and the architecture normalised so that
  # Windows' AMD64 and Linux' x86_64 do not produce two names for one machine
  # shape. Windows keeps "Windows-x64" verbatim: release.yml globs it.
  if(PN_ARCH MATCHES "^(AMD64|amd64|x86_64|x64)$")
    set(arch "x64")
  elseif(PN_ARCH MATCHES "^(aarch64|arm64|ARM64)$")
    set(arch "arm64")
  else()
    set(arch "${PN_ARCH}")
  endif()

  if(PN_SYSTEM STREQUAL "Windows")
    set(platform "Windows-${arch}")
    # Unchanged from what was verified on real Windows hardware: a portable
    # ZIP and an NSIS installer, sharing one CPACK_PACKAGE_FILE_NAME (CPack
    # appends .zip / .exe itself -- see results/release/0.1.0/).
    set(generators "ZIP;NSIS")
  elseif(PN_SYSTEM STREQUAL "Linux")
    set(platform "Linux-${arch}")
    # TGZ preserves the executable bit and symlinks, which a ZIP does not, and
    # STGZ makes the same tree a self-extracting script. Deliberately NOT DEB
    # or RPM: CPack offers both on this host, but neither has been installed
    # or run anywhere, and an untested .deb is a support claim.
    set(generators "TGZ;STGZ")
  elseif(PN_SYSTEM STREQUAL "Darwin")
    set(platform "macOS-${arch}")
    # TGZ only. No .dmg, no bundle, no code signing, no notarization: nothing
    # has been built or run on macOS, so a .dmg would look like a supported
    # artifact and would not be one.
    set(generators "TGZ")
  else()
    message(FATAL_ERROR
      "cfdapp_package_naming: unsupported system \"${PN_SYSTEM}\". "
      "Packaging is defined for Windows, Linux and Darwin. Add the platform "
      "deliberately -- with something actually built and run on it -- rather "
      "than letting the package take a name nobody has verified.")
  endif()

  set(${PN_OUT_NAME} "CFDApp-${version}-${platform}" PARENT_SCOPE)
  set(${PN_OUT_GENERATORS} "${generators}" PARENT_SCOPE)
endfunction()
