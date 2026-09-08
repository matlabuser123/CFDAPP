#pragma once

#include <cstdlib>
#include <iostream>

// A deliberately tiny check-and-report helper for CFDApp's early unit
// tests, used instead of pulling in a full test framework (see TODO.md's
// P0 -- Buildable Project: "Do not introduce GoogleTest/Catch2 yet").
// Revisit this once the test surface (mesh/fields/algebra numerics) grows
// past what plain CHECK-per-line executables handle comfortably.
//
// Each test's main() ends with CFD_TEST_MAIN_END(), which returns
// EXIT_FAILURE if any CFD_CHECK/CFD_CHECK_THROWS in that translation unit
// failed, EXIT_SUCCESS otherwise.

namespace cfd::test {

inline int& failureCount() {
  static int count = 0;
  return count;
}

inline void reportFailure(const char* expr, const char* file, int line) {
  std::cerr << file << ':' << line << ": CHECK failed: " << expr << '\n';
  ++failureCount();
}

}  // namespace cfd::test

// MSVC's C4127 ("conditional expression is constant") fires on the
// `if (!(expr))` below whenever expr happens to be compile-time-constant
// (e.g. a type trait, or a comparison between two `constexpr` values) --
// both of which are common and legitimate in these tests. Suppress just
// that warning around the check itself rather than weakening any target's
// warning set or avoiding constexpr-friendly assertions.
#if defined(_MSC_VER)
#define CFD_CHECK(expr)                                                      \
  do {                                                                       \
    __pragma(warning(push)) __pragma(warning(disable : 4127)) if (!(expr)) { \
      ::cfd::test::reportFailure(#expr, __FILE__, __LINE__);                 \
    }                                                                        \
    __pragma(warning(pop))                                                   \
  } while (false)
#else
#define CFD_CHECK(expr)                                      \
  do {                                                       \
    if (!(expr)) {                                           \
      ::cfd::test::reportFailure(#expr, __FILE__, __LINE__); \
    }                                                        \
  } while (false)
#endif

#define CFD_CHECK_THROWS(expr, ExceptionType)                                          \
  do {                                                                                 \
    bool cfd_threw = false;                                                            \
    try {                                                                              \
      (void)(expr);                                                                    \
    } catch (const ExceptionType&) {                                                   \
      cfd_threw = true;                                                                \
    }                                                                                  \
    if (!cfd_threw) {                                                                  \
      ::cfd::test::reportFailure(#expr " throws " #ExceptionType, __FILE__, __LINE__); \
    }                                                                                  \
  } while (false)

#define CFD_TEST_MAIN_END()                                              \
  do {                                                                   \
    if (::cfd::test::failureCount() > 0) {                               \
      std::cerr << ::cfd::test::failureCount() << " check(s) failed.\n"; \
      return EXIT_FAILURE;                                               \
    }                                                                    \
    return EXIT_SUCCESS;                                                 \
  } while (false)
