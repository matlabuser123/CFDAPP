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
  cfd::solver::validateSolverRobustnessSettings(settings.robustness, settings.velocityRelaxation,
                                                settings.pressureRelaxation);
}

cfd::discretization::NonOrthogonalCorrectionOptions nonOrthogonalOptions(
    const SIMPLESettings& settings) noexcept {
  return cfd::discretization::NonOrthogonalCorrectionOptions{settings.nonOrthogonalCorrections > 0,
                                                             settings.gradientScheme};
}

const char* faceFluxSchemeName(FaceFluxScheme scheme) noexcept {
  switch (scheme) {
    case FaceFluxScheme::Automatic:
      return "automatic";
    case FaceFluxScheme::Linear:
      return "linear";
    case FaceFluxScheme::RhieChow:
      return "rhie_chow";
  }
  return "automatic";
}

FaceFluxScheme parseFaceFluxScheme(std::string_view name) {
  if (name == "automatic") return FaceFluxScheme::Automatic;
  if (name == "linear") return FaceFluxScheme::Linear;
  if (name == "rhie_chow") return FaceFluxScheme::RhieChow;
  throw InvalidArgumentError("parseFaceFluxScheme: unknown face flux scheme \"" +
                             std::string(name) + "\" (automatic, linear, rhie_chow)");
}

FaceFluxScheme resolveFaceFluxScheme(FaceFluxScheme scheme, int dimension) noexcept {
  if (scheme != FaceFluxScheme::Automatic) return scheme;
  return dimension == 3 ? FaceFluxScheme::RhieChow : FaceFluxScheme::Linear;
}

}  // namespace cfd::pressure_velocity
