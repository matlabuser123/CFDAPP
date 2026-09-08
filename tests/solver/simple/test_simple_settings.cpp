#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::validateSIMPLESettings;

TEST(SIMPLESettingsTest, DefaultsAreValid) {
  const SIMPLESettings settings;
  EXPECT_NO_THROW(validateSIMPLESettings(settings));
}

TEST(SIMPLESettingsTest, RejectsZeroMaxIterations) {
  SIMPLESettings settings;
  settings.maxIterations = 0;
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}

TEST(SIMPLESettingsTest, RejectsZeroVelocityRelaxation) {
  SIMPLESettings settings;
  settings.velocityRelaxation = 0.0;
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}

TEST(SIMPLESettingsTest, RejectsNegativeVelocityRelaxation) {
  SIMPLESettings settings;
  settings.velocityRelaxation = -0.1;
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}

TEST(SIMPLESettingsTest, RejectsVelocityRelaxationAboveOne) {
  SIMPLESettings settings;
  settings.velocityRelaxation = 1.1;
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}

TEST(SIMPLESettingsTest, AcceptsVelocityRelaxationExactlyOne) {
  SIMPLESettings settings;
  settings.velocityRelaxation = 1.0;
  EXPECT_NO_THROW(validateSIMPLESettings(settings));
}

TEST(SIMPLESettingsTest, RejectsNonFinitePressureRelaxation) {
  SIMPLESettings settings;
  settings.pressureRelaxation = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}

TEST(SIMPLESettingsTest, RejectsZeroVelocityTolerance) {
  SIMPLESettings settings;
  settings.velocityTolerance = 0.0;
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}

TEST(SIMPLESettingsTest, RejectsNegativePressureTolerance) {
  SIMPLESettings settings;
  settings.pressureTolerance = -1e-8;
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}

TEST(SIMPLESettingsTest, RejectsNonFiniteContinuityTolerance) {
  SIMPLESettings settings;
  settings.continuityTolerance = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(validateSIMPLESettings(settings), InvalidArgumentError);
}
