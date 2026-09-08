cmake_minimum_required(VERSION 3.20)

project(CFDApp
  VERSION 0.1.0
  DESCRIPTION "CFDApp - a computational fluid dynamics solver"
  LANGUAGES CXX
)

# C++ standard is a project-wide requirement rather than a per-target
# style choice, so it is set once here instead of being repeated on every
# target.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Needed by clangd / IDE tooling; also required for CMAKE_BUILD_TYPE to be
# meaningful with the single-config Ninja generator used by the presets.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Suffix appended after the project() version in the string `cfdapp`
# reports, e.g. "0.1.0-dev". Leave empty for a tagged release build.
set(CFDAPP_VERSION_SUFFIX "dev" CACHE STRING
  "Suffix appended after the project version (e.g. dev, rc1); empty for a release build.")

# --- Optional subsystems ---------------------------------------------------
# None of these are required for the baseline build, and none are linked
# into any target yet. Each turns on the corresponding cmake/*.cmake module
# (find_package/enable_language only) but stays OFF until its own phase of
# work starts -- see PROJECT_STRUCTURE.md.
option(CFDAPP_BUILD_GUI     "Build the Qt GUI application (apps/gui)" OFF)
option(CFDAPP_ENABLE_OPENMP "Enable OpenMP acceleration"              OFF)
option(CFDAPP_ENABLE_MPI    "Enable MPI domain decomposition"         OFF)
option(CFDAPP_ENABLE_CUDA   "Enable the CUDA GPU backend"             OFF)

# TODO.md "P1 -- Quality Gate" section 7-9: a build-wide switch, off by
# default (a sanitized build is markedly slower and is a deliberate
# quality-gate pass, not every developer's everyday inner loop) -- see
# cmake/Sanitizers.cmake for what it actually enables per toolchain.
option(CFDAPP_ENABLE_SANITIZERS "Build with AddressSanitizer (+ UBSan on Clang/GCC)" OFF)

include(cmake/CompilerOptions.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Testing.cmake)
include(cmake/Dependencies.cmake)

if(CFDAPP_ENABLE_OPENMP)
  include(cmake/OpenMP.cmake)
endif()

if(CFDAPP_ENABLE_MPI)
  include(cmake/MPI.cmake)
endif()

if(CFDAPP_ENABLE_CUDA)
  include(cmake/CUDA.cmake)
endif()

add_subdirectory(src)
add_subdirectory(apps/cli)

if(CFDAPP_BUILD_GUI)
  message(WARNING
    "CFDAPP_BUILD_GUI is ON, but apps/gui has no build rules yet (GUI is "
    "out of scope for the current build-system phase). Skipping.")
endif()

include(CTest)
enable_testing()
add_subdirectory(tests)
