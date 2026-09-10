#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/core/Exception.hpp"

using cfd::boundary::Adiabatic;
using cfd::boundary::HeatFlux;

TEST(AdiabaticTest, ReturnsOwnerValueUnchanged) {
  const Adiabatic bc;
  EXPECT_DOUBLE_EQ(bc.boundaryValue(310.0, 0.5), 310.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(-12.0, 2.0), -12.0);
}

TEST(AdiabaticTest, EquivalentToZeroHeatFluxForAnyConductivity) {
  const Adiabatic adiabatic;
  const HeatFlux zeroFluxWeak(0.0, 0.1);
  const HeatFlux zeroFluxStrong(0.0, 500.0);
  const cfd::Real owner = 288.0;
  const cfd::Real distance = 0.75;
  EXPECT_DOUBLE_EQ(adiabatic.boundaryValue(owner, distance),
                   zeroFluxWeak.boundaryValue(owner, distance));
  EXPECT_DOUBLE_EQ(adiabatic.boundaryValue(owner, distance),
                   zeroFluxStrong.boundaryValue(owner, distance));
}

TEST(AdiabaticTest, TypeAndNameAreThermalSpecific) {
  const Adiabatic bc;
  EXPECT_EQ(bc.type(), cfd::boundary::BoundaryConditionType::Adiabatic);
  EXPECT_EQ(bc.name(), "Adiabatic");
}

TEST(AdiabaticTest, RejectsInvalidDistance) {
  const Adiabatic bc;
  EXPECT_THROW((void)bc.boundaryValue(1.0, 0.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)bc.boundaryValue(1.0, -1.0), cfd::InvalidArgumentError);
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((void)bc.boundaryValue(1.0, nan), cfd::InvalidArgumentError);
}

TEST(AdiabaticTest, RepeatedEvaluationIsDeterministic) {
  const Adiabatic bc;
  EXPECT_EQ(bc.boundaryValue(275.0, 0.4), bc.boundaryValue(275.0, 0.4));
}
