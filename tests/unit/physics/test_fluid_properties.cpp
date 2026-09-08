#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/physics/FluidProperties.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::physics::FluidProperties;

TEST(FluidPropertiesTest, StoresDensityAndViscosity) {
  const FluidProperties fluid(1.2, 1.8e-5);
  EXPECT_DOUBLE_EQ(fluid.density(), 1.2);
  EXPECT_DOUBLE_EQ(fluid.dynamicViscosity(), 1.8e-5);
}

TEST(FluidPropertiesTest, KinematicViscosityIsMuOverRho) {
  const FluidProperties fluid(1.2, 1.8e-5);
  EXPECT_DOUBLE_EQ(fluid.kinematicViscosity(), 1.8e-5 / 1.2);
}

TEST(FluidPropertiesTest, RejectsZeroDensity) {
  EXPECT_THROW(FluidProperties(0.0, 1.0), InvalidArgumentError);
}

TEST(FluidPropertiesTest, RejectsNegativeDensity) {
  EXPECT_THROW(FluidProperties(-1.0, 1.0), InvalidArgumentError);
}

TEST(FluidPropertiesTest, RejectsZeroViscosity) {
  EXPECT_THROW(FluidProperties(1.0, 0.0), InvalidArgumentError);
}

TEST(FluidPropertiesTest, RejectsNegativeViscosity) {
  EXPECT_THROW(FluidProperties(1.0, -1.0), InvalidArgumentError);
}

TEST(FluidPropertiesTest, RejectsNonFiniteDensity) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(FluidProperties(nan, 1.0), InvalidArgumentError);
  EXPECT_THROW(FluidProperties(inf, 1.0), InvalidArgumentError);
}

TEST(FluidPropertiesTest, RejectsNonFiniteViscosity) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(FluidProperties(1.0, nan), InvalidArgumentError);
  EXPECT_THROW(FluidProperties(1.0, inf), InvalidArgumentError);
}
