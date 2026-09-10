#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/core/Exception.hpp"

using cfd::boundary::FixedTemperature;

TEST(FixedTemperatureTest, StoresAndReturnsExactTemperature) {
  const FixedTemperature bc(310.0);
  EXPECT_DOUBLE_EQ(bc.temperature(), 310.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(200.0, 0.5), 310.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(-999.0, 3.0), 310.0);  // ignores owner value.
}

TEST(FixedTemperatureTest, TypeAndNameAreThermalSpecific) {
  const FixedTemperature bc(300.0);
  EXPECT_EQ(bc.type(), cfd::boundary::BoundaryConditionType::FixedTemperature);
  EXPECT_EQ(bc.name(), "FixedTemperature");
}

TEST(FixedTemperatureTest, RejectsNonFiniteTemperature) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_THROW((FixedTemperature(nan)), cfd::InvalidArgumentError);
  EXPECT_THROW((FixedTemperature(inf)), cfd::InvalidArgumentError);
}

TEST(FixedTemperatureTest, RepeatedEvaluationIsDeterministic) {
  const FixedTemperature bc(288.15);
  const cfd::Real first = bc.boundaryValue(100.0, 1.0);
  const cfd::Real second = bc.boundaryValue(100.0, 1.0);
  EXPECT_EQ(first, second);
}
