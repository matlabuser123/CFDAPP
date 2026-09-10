#include "cfd/turbulence/SSTEquation.hpp"

#include <algorithm>
#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::turbulence {

namespace {

// Minimum value computeCrossDiffusionCoefficient clamps to -- the
// standard small-positive protection value used throughout the SST
// literature/reference implementations (e.g. Menter's own papers, most
// public SST codes) so F1's own arg1 division never sees a zero or
// negative denominator.
constexpr Real kMinimumCrossDiffusionCoefficient = 1e-10;

}  // namespace

Real computeSSTAlpha(Real beta, Real sigmaOmega, Real betaStar, Real kappa) {
  if (!std::isfinite(betaStar) || !(betaStar > 0.0)) {
    throw InvalidArgumentError("computeSSTAlpha: betaStar must be finite and > 0");
  }
  return (beta / betaStar) - (sigmaOmega * kappa * kappa / std::sqrt(betaStar));
}

Real blendSSTCoefficient(Real F1, Real phi1, Real phi2) {
  return (F1 * phi1) + ((1.0 - F1) * phi2);
}

Real computeCrossDiffusionCoefficient(Real rho, Real sigmaOmega2, Real omega,
                                      Real gradKDotGradOmega) {
  if (!std::isfinite(omega) || !(omega > 0.0)) {
    throw InvalidArgumentError("computeCrossDiffusionCoefficient: omega must be finite and > 0");
  }
  const Real raw = 2.0 * rho * sigmaOmega2 * gradKDotGradOmega / omega;
  return std::max(raw, kMinimumCrossDiffusionCoefficient);
}

Real computeF1Argument(Real k, Real omega, Real wallDistance, Real kinematicViscosity, Real rho,
                       Real betaStar, Real sigmaOmega2, Real crossDiffusionCoefficient) {
  if (!std::isfinite(omega) || !(omega > 0.0)) {
    throw InvalidArgumentError("computeF1Argument: omega must be finite and > 0");
  }
  if (!std::isfinite(wallDistance) || !(wallDistance > 0.0)) {
    throw InvalidArgumentError("computeF1Argument: wallDistance must be finite and > 0");
  }
  if (!std::isfinite(crossDiffusionCoefficient) || !(crossDiffusionCoefficient > 0.0)) {
    throw InvalidArgumentError(
        "computeF1Argument: crossDiffusionCoefficient must be finite and > 0");
  }
  const Real kSafe = std::max(k, 0.0);
  const Real y2 = wallDistance * wallDistance;
  const Real term1 = std::sqrt(kSafe) / (betaStar * omega * wallDistance);
  const Real term2 = 500.0 * kinematicViscosity / (y2 * omega);
  const Real term3 = 4.0 * rho * sigmaOmega2 * kSafe / (crossDiffusionCoefficient * y2);
  return std::min(std::max(term1, term2), term3);
}

Real computeF1(Real arg1) {
  const Real arg1Sq = arg1 * arg1;
  return std::tanh(arg1Sq * arg1Sq);
}

Real computeF2Argument(Real k, Real omega, Real wallDistance, Real kinematicViscosity,
                       Real betaStar) {
  if (!std::isfinite(omega) || !(omega > 0.0)) {
    throw InvalidArgumentError("computeF2Argument: omega must be finite and > 0");
  }
  if (!std::isfinite(wallDistance) || !(wallDistance > 0.0)) {
    throw InvalidArgumentError("computeF2Argument: wallDistance must be finite and > 0");
  }
  const Real kSafe = std::max(k, 0.0);
  const Real y2 = wallDistance * wallDistance;
  const Real term1 = 2.0 * std::sqrt(kSafe) / (betaStar * omega * wallDistance);
  const Real term2 = 500.0 * kinematicViscosity / (y2 * omega);
  return std::max(term1, term2);
}

Real computeF2(Real arg2) { return std::tanh(arg2 * arg2); }

Real computeCrossDiffusionSource(Real rho, Real sigmaOmega2, Real omega, Real gradKDotGradOmega,
                                 Real F1) {
  if (!std::isfinite(omega) || !(omega > 0.0)) {
    throw InvalidArgumentError("computeCrossDiffusionSource: omega must be finite and > 0");
  }
  return 2.0 * (1.0 - F1) * rho * sigmaOmega2 * gradKDotGradOmega / omega;
}

Real computeSSTTurbulentViscosity(Real rho, Real a1, Real k, Real omega, Real strainMagnitude,
                                  Real F2) {
  if (!std::isfinite(a1) || !(a1 > 0.0)) {
    throw InvalidArgumentError("computeSSTTurbulentViscosity: a1 must be finite and > 0");
  }
  if (!std::isfinite(omega) || !(omega > 0.0)) {
    throw InvalidArgumentError("computeSSTTurbulentViscosity: omega must be finite and > 0");
  }
  if (!std::isfinite(strainMagnitude) || strainMagnitude < 0.0) {
    throw InvalidArgumentError(
        "computeSSTTurbulentViscosity: strainMagnitude must be finite and >= 0");
  }
  const Real kSafe = std::max(k, 0.0);
  const Real denominator = std::max(a1 * omega, strainMagnitude * F2);
  return rho * a1 * kSafe / denominator;
}

Real limitProduction(Real productionK, Real productionLimiterFactor, Real betaStar, Real rho,
                     Real k, Real omega) {
  if (!std::isfinite(productionLimiterFactor) || !(productionLimiterFactor > 0.0)) {
    throw InvalidArgumentError("limitProduction: productionLimiterFactor must be finite and > 0");
  }
  if (!std::isfinite(betaStar) || !(betaStar > 0.0)) {
    throw InvalidArgumentError("limitProduction: betaStar must be finite and > 0");
  }
  const Real limit = productionLimiterFactor * betaStar * rho * k * omega;
  return std::min(productionK, limit);
}

}  // namespace cfd::turbulence
