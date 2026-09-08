# Reusable compiler-warning configuration.
#
# Exposed as an INTERFACE library rather than global CMAKE_CXX_FLAGS so each
# target opts in explicitly via target_link_libraries(<tgt> PRIVATE
# cfdapp::compiler_warnings), and consumers of a library target never
# inherit its warning flags.

add_library(cfdapp_compiler_warnings INTERFACE)
add_library(cfdapp::compiler_warnings ALIAS cfdapp_compiler_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cfdapp_compiler_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
  )
elseif(MSVC)
  target_compile_options(cfdapp_compiler_warnings INTERFACE
    /W4
    /permissive-
    # MSVC-equivalent set of the extra useful warnings /W4 misses, taken
    # from the commonly used "cpp best practices" MSVC warning list.
    /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311 /w14545
    /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905
    /w14906 /w14928
    # Not warning flags, but required MSVC codegen options: the Visual
    # Studio generator's default CXX flags include these, but the Ninja
    # generator used by our presets does not, so without them <iostream>
    # et al. trip C4530/C4577 (exception unwind semantics not enabled)
    # and RTTI-using code fails to link correctly.
    /EHsc
    /GR
  )
else()
  message(STATUS
    "CFDApp: no curated warning set for compiler "
    "'${CMAKE_CXX_COMPILER_ID}' yet; building with compiler defaults.")
endif()

# -Werror is intentionally not enabled: the codebase is still small and
# growing quickly, and failing the build on every new warning would slow
# down early scaffolding more than it would help. Revisit once the core
# solver is stable (see PROJECT_STRUCTURE.md).
