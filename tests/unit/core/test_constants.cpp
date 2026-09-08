#include <gtest/gtest.h>

#include "cfd/core/Constants.hpp"

TEST(CoreConstants, PiIsCorrect) { EXPECT_DOUBLE_EQ(cfd::constants::pi, 3.14159265358979323846); }

TEST(CoreConstants, DerivedAnglesAreConsistent) {
  EXPECT_DOUBLE_EQ(cfd::constants::twoPi, 2.0 * cfd::constants::pi);
  EXPECT_DOUBLE_EQ(cfd::constants::halfPi, cfd::constants::pi / 2.0);
}

TEST(CoreConstants, ToleranceOrdering) {
  EXPECT_GT(cfd::constants::tiny, 0.0);
  EXPECT_GT(cfd::constants::small, cfd::constants::tiny);
}
