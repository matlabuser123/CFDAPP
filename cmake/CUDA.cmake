# Included only when CFDAPP_ENABLE_CUDA=ON (see root CMakeLists.txt).
# No CFDApp target compiles CUDA yet (Phase 6, see PROJECT_STRUCTURE.md);
# this module only makes the option safe to turn on ahead of that without
# blocking the baseline build.

include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
  # Choose the target architectures BEFORE enable_language(CUDA).
  #
  # CUDA-QUAL-001 (2026-09-18) found that cuda/CMakeLists.txt's
  # "if(NOT CMAKE_CUDA_ARCHITECTURES) set(... 80)" never took effect:
  # enable_language(CUDA) initialises CMAKE_CUDA_ARCHITECTURES from nvcc's
  # own default first (sm_52 on CUDA 11.x and 12.x), so the guard was always
  # false. Every GPU build this project made was therefore sm_52 cubin plus
  # compute_52 PTX, and ran on the Ada development GPU only through the
  # driver's PTX JIT -- not the sm_80 target the comment claimed.
  #
  # An explicit -DCMAKE_CUDA_ARCHITECTURES=... or the CUDAARCHS environment
  # variable still wins; this only replaces nvcc's default.
  if(NOT DEFINED CACHE{CMAKE_CUDA_ARCHITECTURES} AND NOT DEFINED ENV{CUDAARCHS})
    execute_process(COMMAND "${CMAKE_CUDA_COMPILER}" --version
                    OUTPUT_VARIABLE _cfdapp_nvcc_version_text
                    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_cfdapp_nvcc_version_text MATCHES "release ([0-9]+)\\.([0-9]+)")
      set(_cfdapp_nvcc_version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
    else()
      set(_cfdapp_nvcc_version "0.0")
    endif()
    # CUDA >= 11.8 is the first that can emit cubin native to Ada, sm_89
    # (NVIDIA Ada Compatibility Guide). Older toolkits reject sm_89 outright,
    # so they keep Ampere only and reach newer GPUs through its PTX.
    if(_cfdapp_nvcc_version VERSION_GREATER_EQUAL 11.8)
      set(CMAKE_CUDA_ARCHITECTURES 80 89)
    else()
      set(CMAKE_CUDA_ARCHITECTURES 80)
    endif()
    message(STATUS
      "CFDApp: nvcc ${_cfdapp_nvcc_version} -> CUDA architectures "
      "${CMAKE_CUDA_ARCHITECTURES} (nvcc's own default is not used)")
  endif()

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
