// P2-TURB-004 section 28: the classical standard k-epsilon constants
// (Launder & Spalding 1974), exactly.
#include <gtest/gtest.h>

#include "cfd/turbulence/KEpsilonCoefficients.hpp"

using cfd::turbulence::KEpsilonCoefficients;

TEST(KEpsilonCoefficientsTest, DefaultsMatchStandardModel) {
  const KEpsilonCoefficients c;
  EXPECT_EQ(c.cMu, 0.09);
  EXPECT_EQ(c.c1Epsilon, 1.44);
  EXPECT_EQ(c.c2Epsilon, 1.92);
  EXPECT_EQ(c.sigmaK, 1.0);
  EXPECT_EQ(c.sigmaEpsilon, 1.3);
}
