cmake_minimum_required(VERSION 3.20)

project(CFDApp
  VERSION 0.2.0
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

# P13-004 -- build identity for exported results. A metadata.json that cannot
# say which build produced it cannot be used to reproduce or interpret a run,
# which is what the result metadata is for.
#
# Captured at configure time, and "unknown" is a legitimate answer: a build
# from an exported tarball has no git, and a build from a dirty tree is marked
# as such rather than claiming a commit it does not match. Saying "unknown"
# honestly is the point -- a metadata file that guessed would be worse than
# one that admits it does not know.
#
# ERROR_QUIET and the fallback mean a machine without git configures exactly as
# before. The value is fixed per configure, so it never makes two runs of the
# same binary differ -- which criterion M6, and P13-003's E8, both depend on.
set(CFDAPP_BUILD_IDENTITY "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE CFDAPP_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(CFDAPP_GIT_SHA)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE CFDAPP_GIT_DIRTY
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(CFDAPP_GIT_DIRTY)
      set(CFDAPP_BUILD_IDENTITY "${CFDAPP_GIT_SHA}-modified")
    else()
      set(CFDAPP_BUILD_IDENTITY "${CFDAPP_GIT_SHA}")
    endif()
  endif()
endif()
message(STATUS "CFDApp build identity: ${CFDAPP_BUILD_IDENTITY}")

# --- Optional subsystems ---------------------------------------------------
# None of these are required for the baseline build, and none are linked
# into any target yet. Each turns on the corresponding cmake/*.cmake module
# (find_package/enable_language only) but stays OFF until its own phase of
# work starts -- see PROJECT_STRUCTURE.md.
option(CFDAPP_BUILD_GUI     "Build the Qt GUI application (apps/gui)" OFF)
option(CFDAPP_ENABLE_OPENMP "Enable OpenMP acceleration"              OFF)
option(CFDAPP_ENABLE_MPI    "Enable MPI domain decomposition"         OFF)
option(CFDAPP_ENABLE_CUDA   "Enable the CUDA GPU backend"             OFF)
# P5-J -- Packaging: CPack config for a Windows ZIP/NSIS package of the
# GUI build (cmake/Packaging.cmake) -- see
# docs/developer_guide/packaging.md for this configuration's own
# disclosed, unverified-on-Windows status before relying on it.
option(CFDAPP_ENABLE_PACKAGING "Enable CPack packaging (cmake/Packaging.cmake)" OFF)

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

if(CFDAPP_ENABLE_CUDA)
  add_subdirectory(cuda)
endif()

add_subdirectory(src)
add_subdirectory(apps/cli)

include(CTest)
enable_testing()
add_subdirectory(tests)

# apps/gui is added only after enable_testing() (immediately above) --
# gtest_discover_tests() calls inside apps/gui/CMakeLists.txt (P5-B
# section 59's own controller-level tests) are otherwise silently never
# registered with CTest: a subdirectory add_subdirectory()'d *before*
# enable_testing() never gets its CTestTestfile.cmake included in the
# aggregate one ctest actually reads, even though the test binary itself
# still builds and passes fine when run directly -- caught by exactly
# that discrepancy (`ctest -N` not listing SimulationControllerTest.*
# despite the binary passing standalone) while finishing P5.
if(CFDAPP_BUILD_GUI)
  add_subdirectory(apps/gui)
endif()

if(CFDAPP_ENABLE_PACKAGING)
  include(cmake/Packaging.cmake)
endif()

# P4 -- Performance: standalone benchmark tooling (not part of the CTest
# suite -- see benchmarks/CMakeLists.txt's own header comment).
option(CFDAPP_BUILD_BENCHMARKS "Build the standalone performance-benchmark executables" ON)
if(CFDAPP_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
