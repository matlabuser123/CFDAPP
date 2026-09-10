#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/thermal/ThermalProperties.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::thermal::ThermalProperties;

TEST(ThermalPropertiesTest, StoresConductivityAndSpecificHeat) {
  const ThermalProperties thermal(0.6, 4180.0);
  EXPECT_DOUBLE_EQ(thermal.conductivity(), 0.6);
  EXPECT_DOUBLE_EQ(thermal.specificHeat(), 4180.0);
}

TEST(ThermalPropertiesTest, ThermalDiffusivityIsKOverRhoCp) {
  const ThermalProperties thermal(0.6, 4180.0);
  const Real density = 1000.0;
  EXPECT_DOUBLE_EQ(thermal.thermalDiffusivity(density), 0.6 / (1000.0 * 4180.0));
}

TEST(ThermalPropertiesTest, RejectsZeroConductivity) {
  EXPECT_THROW(ThermalProperties(0.0, 1.0), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, RejectsNegativeConductivity) {
  EXPECT_THROW(ThermalProperties(-1.0, 1.0), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, RejectsZeroSpecificHeat) {
  EXPECT_THROW(ThermalProperties(1.0, 0.0), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, RejectsNegativeSpecificHeat) {
  EXPECT_THROW(ThermalProperties(1.0, -1.0), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, RejectsNonFiniteConductivity) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(ThermalProperties(nan, 1.0), InvalidArgumentError);
  EXPECT_THROW(ThermalProperties(inf, 1.0), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, RejectsNonFiniteSpecificHeat) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(ThermalProperties(1.0, nan), InvalidArgumentError);
  EXPECT_THROW(ThermalProperties(1.0, inf), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, ThermalDiffusivityRejectsZeroDensity) {
  const ThermalProperties thermal(0.6, 4180.0);
  EXPECT_THROW((void)thermal.thermalDiffusivity(0.0), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, ThermalDiffusivityRejectsNegativeDensity) {
  const ThermalProperties thermal(0.6, 4180.0);
  EXPECT_THROW((void)thermal.thermalDiffusivity(-1.0), InvalidArgumentError);
}

TEST(ThermalPropertiesTest, ThermalDiffusivityRejectsNonFiniteDensity) {
  const ThermalProperties thermal(0.6, 4180.0);
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)thermal.thermalDiffusivity(nan), InvalidArgumentError);
  EXPECT_THROW((void)thermal.thermalDiffusivity(inf), InvalidArgumentError);
}
