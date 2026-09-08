# Included only when CFDAPP_ENABLE_CUDA=ON (see root CMakeLists.txt).
# No CFDApp target compiles CUDA yet (Phase 6, see PROJECT_STRUCTURE.md);
# this module only makes the option safe to turn on ahead of that without
# blocking the baseline build.

include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 20)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
else()
  message(WARNING
    "CFDAPP_ENABLE_CUDA is ON but no CUDA compiler was found; continuing "
    "without it.")
  set(CFDAPP_ENABLE_CUDA OFF CACHE BOOL "Enable the CUDA GPU backend" FORCE)
endif()
