// P2-TURB-006 section 5/59: Menter's original (1994) SST coefficient
// set, exactly.
#include <gtest/gtest.h>

#include "cfd/turbulence/SSTCoefficients.hpp"

using cfd::turbulence::SSTCoefficients;

TEST(SSTCoefficientsTest, DefaultsMatchStandardModel) {
  const SSTCoefficients c;
  EXPECT_DOUBLE_EQ(c.betaStar, 0.09);
  EXPECT_DOUBLE_EQ(c.a1, 0.31);
  EXPECT_DOUBLE_EQ(c.kappa, 0.41);

  EXPECT_DOUBLE_EQ(c.sigmaK1, 0.85);
  EXPECT_DOUBLE_EQ(c.sigmaOmega1, 0.5);
  EXPECT_DOUBLE_EQ(c.beta1, 0.075);

  EXPECT_DOUBLE_EQ(c.sigmaK2, 1.0);
  EXPECT_DOUBLE_EQ(c.sigmaOmega2, 0.856);
  EXPECT_DOUBLE_EQ(c.beta2, 0.0828);

  EXPECT_DOUBLE_EQ(c.productionLimiterFactor, 10.0);
}
