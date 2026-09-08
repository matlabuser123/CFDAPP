#include <gtest/gtest.h>

#include <type_traits>

#include "cfd/core/Types.hpp"

static_assert(std::is_floating_point_v<cfd::Real>);
static_assert(std::is_unsigned_v<cfd::Index>);
static_assert(std::is_signed_v<cfd::SignedIndex>);

TEST(CoreTypes, AliasesAreUsable) {
  // The static_asserts above are the real check; this exists so CTest has
  // a runtime result to report (a translation unit with only
  // static_asserts and no TEST would not be discovered by
  // gtest_discover_tests).
  const cfd::Real real_value = 1.5;
  const cfd::Index index_value = 3;
  const cfd::SignedIndex signed_value = -3;

  EXPECT_GT(real_value, 0.0);
  EXPECT_EQ(index_value, 3U);
  EXPECT_LT(signed_value, 0);
}
