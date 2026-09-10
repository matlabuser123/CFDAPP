#include "cfd/pressure_velocity/SIMPLESettings.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::pressure_velocity {

namespace {

void validateRelaxation(Real alpha, const char* name) {
  if (!std::isfinite(alpha) || !(alpha > 0.0) || alpha > 1.0) {
    throw InvalidArgumentError(std::string("validateSIMPLESettings: ") + name +
                               " must be finite and in (0, 1]");
  }
}

void validateTolerance(Real tolerance, const char* name) {
  if (!std::isfinite(tolerance) || !(tolerance > 0.0)) {
    throw InvalidArgumentError(std::string("validateSIMPLESettings: ") + name +
                               " must be finite and > 0");
  }
}

}  // namespace

void validateSIMPLESettings(const SIMPLESettings& settings) {
  if (settings.maxIterations == 0) {
    throw InvalidArgumentError("validateSIMPLESettings: maxIterations must be > 0");
  }
  validateRelaxation(settings.velocityRelaxation, "velocityRelaxation");
  validateRelaxation(settings.pressureRelaxation, "pressureRelaxation");
  validateTolerance(settings.velocityTolerance, "velocityTolerance");
  validateTolerance(settings.pressureTolerance, "pressureTolerance");
  validateTolerance(settings.continuityTolerance, "continuityTolerance");
  validateTolerance(settings.turbulenceTolerance, "turbulenceTolerance");
}

}  // namespace cfd::pressure_velocity
