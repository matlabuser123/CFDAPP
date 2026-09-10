#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/core/Exception.hpp"

using cfd::boundary::HeatFlux;

// Hand-derived sign test (this file's canonical worked example, also
// quoted in HeatFlux.hpp's own header comment): k=2 W/(m K), q''=+10
// W/m^2 => dT/dn = -q''/k = -10/2 = -5 K/m. Positive heatFlux means heat
// LEAVING the domain along the outward normal, so temperature must fall
// moving outward -- a negative gradient, exactly as computed.
TEST(HeatFluxTest, PositiveFluxGivesHandDerivedNegativeGradient) {
  const HeatFlux bc(/*heatFlux=*/10.0, /*conductivity=*/2.0);
  const cfd::Real ownerValue = 300.0;
  const cfd::Real distance = 0.5;
  // dT/dn = -5 -> boundaryValue = 300 + (-5)*0.5 = 297.5.
  EXPECT_DOUBLE_EQ(bc.boundaryValue(ownerValue, distance), 297.5);
}

TEST(HeatFluxTest, NegativeFluxGivesPositiveGradient) {
  // Heat entering the domain (negative q'') must raise temperature
  // moving outward -- the mirror image of the positive-flux case.
  const HeatFlux bc(/*heatFlux=*/-10.0, /*conductivity=*/2.0);
  const cfd::Real ownerValue = 300.0;
  const cfd::Real distance = 0.5;
  // dT/dn = -(-10)/2 = +5 -> boundaryValue = 300 + 5*0.5 = 302.5.
  EXPECT_DOUBLE_EQ(bc.boundaryValue(ownerValue, distance), 302.5);
}

TEST(HeatFluxTest, ZeroFluxReturnsOwnerValue) {
  const HeatFlux bc(0.0, 5.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(288.0, 1.0), 288.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(-3.0, 2.0), -3.0);
}

TEST(HeatFluxTest, ConductivityScalesTheGradientInversely) {
  // Same heat flux, double the conductivity -> half the gradient
  // magnitude (a better conductor needs a shallower gradient to carry
  // the same flux).
  const HeatFlux weakConductor(10.0, 2.0);
  const HeatFlux strongConductor(10.0, 4.0);
  const cfd::Real owner = 100.0;
  const cfd::Real distance = 1.0;
  const cfd::Real weakDrop = owner - weakConductor.boundaryValue(owner, distance);
  const cfd::Real strongDrop = owner - strongConductor.boundaryValue(owner, distance);
  EXPECT_DOUBLE_EQ(strongDrop, weakDrop / 2.0);
}

TEST(HeatFluxTest, ExposesHeatFluxAndConductivity) {
  const HeatFlux bc(7.5, 3.0);
  EXPECT_DOUBLE_EQ(bc.heatFlux(), 7.5);
  EXPECT_DOUBLE_EQ(bc.conductivity(), 3.0);
}

TEST(HeatFluxTest, TypeAndNameAreThermalSpecific) {
  const HeatFlux bc(0.0, 1.0);
  EXPECT_EQ(bc.type(), cfd::boundary::BoundaryConditionType::HeatFlux);
  EXPECT_EQ(bc.name(), "HeatFlux");
}

TEST(HeatFluxTest, RejectsInvalidConductivity) {
  EXPECT_THROW((HeatFlux(1.0, 0.0)), cfd::InvalidArgumentError);
  EXPECT_THROW((HeatFlux(1.0, -1.0)), cfd::InvalidArgumentError);
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_THROW((HeatFlux(1.0, nan)), cfd::InvalidArgumentError);
  EXPECT_THROW((HeatFlux(1.0, inf)), cfd::InvalidArgumentError);
}

TEST(HeatFluxTest, RejectsNonFiniteHeatFlux) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_THROW((HeatFlux(nan, 1.0)), cfd::InvalidArgumentError);
  EXPECT_THROW((HeatFlux(inf, 1.0)), cfd::InvalidArgumentError);
}

TEST(HeatFluxTest, RejectsInvalidDistance) {
  const HeatFlux bc(5.0, 1.0);
  EXPECT_THROW((void)bc.boundaryValue(1.0, 0.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)bc.boundaryValue(1.0, -1.0), cfd::InvalidArgumentError);
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((void)bc.boundaryValue(1.0, nan), cfd::InvalidArgumentError);
}

TEST(HeatFluxTest, RepeatedEvaluationIsDeterministic) {
  const HeatFlux bc(12.0, 3.0);
  EXPECT_EQ(bc.boundaryValue(250.0, 0.25), bc.boundaryValue(250.0, 0.25));
}
