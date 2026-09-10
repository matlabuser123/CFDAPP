// P2-TURB-006 section 28: cfd::boundary::WallOmega -- the standard
// (Wilcox) near-wall asymptotic omega value, omega_wall = 60*nu/(beta1*y^2).
#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/WallOmega.hpp"
#include "cfd/core/Exception.hpp"

using cfd::boundary::WallOmega;

// Hand-derived value test: nu=1.5e-5 m^2/s, beta1=0.075, y=0.01 m ->
// omega_wall = 60*1.5e-5/(0.075*0.0001) = 9e-4/7.5e-6 = 120 [1/s].
TEST(WallOmegaTest, HandDerivedValueMatchesWilcoxFormula) {
  const WallOmega bc(/*kinematicViscosity=*/1.5e-5, /*beta1=*/0.075);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(/*ownerValue=*/999.0, /*normalDistance=*/0.01), 120.0);
}

TEST(WallOmegaTest, IgnoresOwnerValue) {
  const WallOmega bc(1.5e-5, 0.075);
  EXPECT_EQ(bc.boundaryValue(0.0, 0.01), bc.boundaryValue(1e6, 0.01));
}

TEST(WallOmegaTest, SmallerDistanceGivesLargerOmega) {
  // omega_wall ~ 1/y^2 -- halving the distance quadruples omega.
  const WallOmega bc(1.5e-5, 0.075);
  const cfd::Real far = bc.boundaryValue(0.0, 0.02);
  const cfd::Real near = bc.boundaryValue(0.0, 0.01);
  EXPECT_NEAR(near, far * 4.0, 1e-9);
}

TEST(WallOmegaTest, ExposesKinematicViscosityAndBeta1) {
  const WallOmega bc(2.0e-5, 0.08);
  EXPECT_DOUBLE_EQ(bc.kinematicViscosity(), 2.0e-5);
  EXPECT_DOUBLE_EQ(bc.beta1(), 0.08);
}

TEST(WallOmegaTest, TypeAndNameAreSSTSpecific) {
  const WallOmega bc(1.5e-5, 0.075);
  EXPECT_EQ(bc.type(), cfd::boundary::BoundaryConditionType::WallOmega);
  EXPECT_EQ(bc.name(), "WallOmega");
}

TEST(WallOmegaTest, RejectsInvalidKinematicViscosity) {
  EXPECT_THROW((WallOmega(0.0, 0.075)), cfd::InvalidArgumentError);
  EXPECT_THROW((WallOmega(-1.0, 0.075)), cfd::InvalidArgumentError);
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_THROW((WallOmega(nan, 0.075)), cfd::InvalidArgumentError);
  EXPECT_THROW((WallOmega(inf, 0.075)), cfd::InvalidArgumentError);
}

TEST(WallOmegaTest, RejectsInvalidBeta1) {
  EXPECT_THROW((WallOmega(1.5e-5, 0.0)), cfd::InvalidArgumentError);
  EXPECT_THROW((WallOmega(1.5e-5, -1.0)), cfd::InvalidArgumentError);
}

TEST(WallOmegaTest, RejectsInvalidDistance) {
  const WallOmega bc(1.5e-5, 0.075);
  EXPECT_THROW((void)bc.boundaryValue(0.0, 0.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)bc.boundaryValue(0.0, -1.0), cfd::InvalidArgumentError);
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((void)bc.boundaryValue(0.0, nan), cfd::InvalidArgumentError);
}

TEST(WallOmegaTest, RepeatedEvaluationIsDeterministic) {
  const WallOmega bc(1.5e-5, 0.075);
  EXPECT_EQ(bc.boundaryValue(0.0, 0.01), bc.boundaryValue(0.0, 0.01));
}
