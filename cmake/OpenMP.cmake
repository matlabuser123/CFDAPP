# Included only when CFDAPP_ENABLE_OPENMP=ON (see root CMakeLists.txt).
# No CFDApp target links against OpenMP yet (Phase 5, see
# PROJECT_STRUCTURE.md); this module only makes the option safe to turn on
# ahead of that without blocking the baseline build.

find_package(OpenMP COMPONENTS CXX)

if(NOT OpenMP_CXX_FOUND)
  message(WARNING
    "CFDAPP_ENABLE_OPENMP is ON but no OpenMP CXX implementation was "
    "found; continuing without it.")
  set(CFDAPP_ENABLE_OPENMP OFF CACHE BOOL "Enable OpenMP acceleration" FORCE)
endif()
