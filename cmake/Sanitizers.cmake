# TODO.md "P1 -- Quality Gate" section 7-9: AddressSanitizer support,
# centralized here (not scattered per-target) exactly as section 9 asks,
# exposed the same way cmake/CompilerOptions.cmake exposes warnings --
# an INTERFACE library targets opt into explicitly.
#
# UndefinedBehaviorSanitizer is NOT available here: UBSan is a Clang/GCC
# feature with no MSVC equivalent (this project's only configured
# compiler toolchain -- see CMakeLists.txt/CompilerOptions.cmake). MSVC
# has shipped AddressSanitizer since VS 2019 16.9; there is no MSVC
# -fsanitize=undefined. Running UBSan on this codebase would require
# building with clang-cl or a full Clang toolchain instead of cl.exe --
# a real toolchain change out of scope for this pass. Document this
# rather than silently skip it (section 43: do not mask a gate item,
# explain why it does not apply here).
add_library(cfdapp_sanitizers INTERFACE)
add_library(cfdapp::sanitizers ALIAS cfdapp_sanitizers)

if(CFDAPP_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(cfdapp_sanitizers INTERFACE /fsanitize=address)
    # /RTC1 (Debug's default runtime checks, added to CMAKE_CXX_FLAGS_DEBUG
    # by CMake itself, not by this project) is incompatible with
    # /fsanitize=address -- cl.exe rejects the combination outright, and
    # there is no "/RTC-" flag to override it back off again (D9002:
    # unknown option). It has to be removed from the flag string itself.
    # This file is include()'d directly from the root CMakeLists.txt (not
    # a function/subdirectory), so a plain set() here already lands in
    # the scope every later add_subdirectory() inherits from -- no
    # PARENT_SCOPE needed (that only pops a value *up* out of a child
    # scope, the reverse of what's needed here).
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    message(STATUS
      "CFDApp: AddressSanitizer enabled (MSVC). UndefinedBehaviorSanitizer "
      "is not available on this toolchain -- see cmake/Sanitizers.cmake.")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cfdapp_sanitizers INTERFACE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
      -fno-sanitize-recover=all
    )
    target_link_options(cfdapp_sanitizers INTERFACE -fsanitize=address,undefined)
    message(STATUS "CFDApp: AddressSanitizer + UndefinedBehaviorSanitizer enabled (${CMAKE_CXX_COMPILER_ID}).")
  else()
    message(WARNING
      "CFDAPP_ENABLE_SANITIZERS is ON, but no sanitizer support is configured for "
      "compiler '${CMAKE_CXX_COMPILER_ID}'. Building without sanitizers.")
  endif()
endif()
