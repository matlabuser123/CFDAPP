# Included only when CFDAPP_ENABLE_MPI=ON (see root CMakeLists.txt).
# No CFDApp target links against MPI yet (Phase 5, see
# PROJECT_STRUCTURE.md); this module only makes the option safe to turn on
# ahead of that without blocking the baseline build.

find_package(MPI COMPONENTS CXX)

if(NOT MPI_CXX_FOUND)
  message(WARNING
    "CFDAPP_ENABLE_MPI is ON but no MPI CXX implementation was found; "
    "continuing without it.")
  set(CFDAPP_ENABLE_MPI OFF CACHE BOOL "Enable MPI domain decomposition" FORCE)
endif()
