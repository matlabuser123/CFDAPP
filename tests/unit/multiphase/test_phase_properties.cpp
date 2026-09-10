// P3-PHYS-005: PhaseProperties -- identity + constant density/viscosity
// (section 21).
#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/multiphase/PhaseProperties.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::multiphase::PhaseProperties;

TEST(PhasePropertiesTest, StoresNameDensityAndViscosity) {
  const PhaseProperties phase("water", 1000.0, 0.001);
  EXPECT_EQ(phase.name(), "water");
  EXPECT_DOUBLE_EQ(phase.density(), 1000.0);
  EXPECT_DOUBLE_EQ(phase.viscosity(), 0.001);
}

TEST(PhasePropertiesTest, RejectsEmptyName) {
  EXPECT_THROW(PhaseProperties("", 1000.0, 0.001), InvalidArgumentError);
}

TEST(PhasePropertiesTest, RejectsZeroOrNegativeDensity) {
  EXPECT_THROW(PhaseProperties("water", 0.0, 0.001), InvalidArgumentError);
  EXPECT_THROW(PhaseProperties("water", -1.0, 0.001), InvalidArgumentError);
}

TEST(PhasePropertiesTest, RejectsZeroOrNegativeViscosity) {
  EXPECT_THROW(PhaseProperties("water", 1000.0, 0.0), InvalidArgumentError);
  EXPECT_THROW(PhaseProperties("water", 1000.0, -0.001), InvalidArgumentError);
}

TEST(PhasePropertiesTest, RejectsNonFiniteDensityOrViscosity) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(PhaseProperties("water", nan, 0.001), InvalidArgumentError);
  EXPECT_THROW(PhaseProperties("water", inf, 0.001), InvalidArgumentError);
  EXPECT_THROW(PhaseProperties("water", 1000.0, nan), InvalidArgumentError);
  EXPECT_THROW(PhaseProperties("water", 1000.0, inf), InvalidArgumentError);
}

TEST(PhasePropertiesTest, TwoPhasesKeepIndependentIdentity) {
  const PhaseProperties water("water", 1000.0, 0.001);
  const PhaseProperties air("air", 1.0, 1.8e-5);
  EXPECT_NE(water.name(), air.name());
  EXPECT_NE(water.density(), air.density());
  EXPECT_NE(water.viscosity(), air.viscosity());
}
