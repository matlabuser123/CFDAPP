# Included only when CFDAPP_ENABLE_CUDA=ON (see root CMakeLists.txt).
# No CFDApp target compiles CUDA yet (Phase 6, see PROJECT_STRUCTURE.md);
# this module only makes the option safe to turn on ahead of that without
# blocking the baseline build.

include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  # nvcc 11.5 (this project's own development toolchain) does not support
  # C++20 -- 17 is both the highest this toolchain accepts and enough for
  # the CSR SpMV kernel's own needs (P4 -- Performance, section 32).
  set(CMAKE_CUDA_STANDARD 17)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
  find_package(CUDAToolkit REQUIRED)
else()
  message(WARNING
    "CFDAPP_ENABLE_CUDA is ON but no CUDA compiler was found; continuing "
    "without it.")
  set(CFDAPP_ENABLE_CUDA OFF CACHE BOOL "Enable the CUDA GPU backend" FORCE)
endif()
