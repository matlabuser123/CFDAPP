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

// P12-NUM-003: the options thermal / species / turbulence diffusion are
// assembled with derive from the same two SIMPLE settings momentum uses.
TEST(SIMPLESettingsTest, NonOrthogonalOptionsFollowTheCorrectionCountAndGradientScheme) {
  cfd::pressure_velocity::SIMPLESettings settings;
  EXPECT_FALSE(cfd::pressure_velocity::nonOrthogonalOptions(settings).enabled);
  settings.nonOrthogonalCorrections = 1;
  settings.gradientScheme = cfd::discretization::GradientScheme::LeastSquares;
  const auto options = cfd::pressure_velocity::nonOrthogonalOptions(settings);
  EXPECT_TRUE(options.enabled);
  EXPECT_EQ(options.gradientScheme, cfd::discretization::GradientScheme::LeastSquares);
  settings.nonOrthogonalCorrections = 5;
  EXPECT_TRUE(cfd::pressure_velocity::nonOrthogonalOptions(settings).enabled);
}
