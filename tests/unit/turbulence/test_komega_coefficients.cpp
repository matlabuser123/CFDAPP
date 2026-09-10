// P2-TURB-005 section 29: the classical standard (Wilcox) k-omega
// constants, exactly.
#include <gtest/gtest.h>

#include "cfd/turbulence/KOmegaCoefficients.hpp"

using cfd::turbulence::KOmegaCoefficients;

TEST(KOmegaCoefficientsTest, DefaultsMatchStandardModel) {
  const KOmegaCoefficients c;
  EXPECT_DOUBLE_EQ(c.betaStar, 0.09);
  EXPECT_DOUBLE_EQ(c.alpha, 5.0 / 9.0);
  EXPECT_DOUBLE_EQ(c.beta, 0.075);
  EXPECT_DOUBLE_EQ(c.sigmaK, 2.0);
  EXPECT_DOUBLE_EQ(c.sigmaOmega, 2.0);
}
