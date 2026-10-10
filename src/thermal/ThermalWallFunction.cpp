#include "cfd/thermal/ThermalWallFunction.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::thermal {

namespace {

void requirePositive(Real value, const char* name, const char* context) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string(context) + ": " + name + " must be finite and > 0");
  }
}

void requireNonNegative(Real value, const char* name, const char* context) {
  if (!std::isfinite(value) || value < 0.0) {
    throw InvalidArgumentError(std::string(context) + ": " + name + " must be finite and >= 0");
  }
}

// The log branch, Pr_t [ln(E y+)/kappa + P]. Split out because both
// temperaturePlus and the crossing-point solve need exactly the same
// expression, and having one copy is what makes them meet at the crossing.
Real logBranch(Real yPlus, Real turbulentPrandtl, Real pFunction,
               const cfd::turbulence::WallFunctionCoefficients& coefficients) {
  return turbulentPrandtl * ((std::log(coefficients.E * yPlus) / coefficients.kappa) + pFunction);
}

}  // namespace

Real jayatillekeP(Real prandtl, Real turbulentPrandtl) {
  constexpr const char* kContext = "jayatillekeP";
  requirePositive(prandtl, "prandtl", kContext);
  requirePositive(turbulentPrandtl, "turbulentPrandtl", kContext);

  const Real ratio = prandtl / turbulentPrandtl;
  // NOT clamped. P is negative for every Pr < Pr_t, which is the ordinary case
  // for air, and forcing it non-negative would change the physics.
  return 9.24 * (std::pow(ratio, 0.75) - 1.0) * (1.0 + (0.28 * std::exp(-0.007 * ratio)));
}

Real thermalSublayerThickness(Real prandtl, Real turbulentPrandtl,
                              const cfd::turbulence::WallFunctionCoefficients& coefficients) {
  constexpr const char* kContext = "thermalSublayerThickness";
  requirePositive(prandtl, "prandtl", kContext);
  requirePositive(turbulentPrandtl, "turbulentPrandtl", kContext);

  const Real pFunction = jayatillekeP(prandtl, turbulentPrandtl);
  // f(y+) = conduction - log. Negative near the wall (the log branch is
  // unbounded below as y+ -> 0, so conduction sits above it... in fact
  // ln(E y+) -> -inf makes the log branch -inf, so f > 0 at tiny y+), and the
  // conduction branch is linear while the log branch grows only
  // logarithmically, so they cross exactly once on (0, inf).
  const auto difference = [&](Real yPlus) {
    return (prandtl * yPlus) - logBranch(yPlus, turbulentPrandtl, pFunction, coefficients);
  };

  // Bracket by expansion rather than by a guessed range: start below any
  // plausible crossing and double until the sign flips.
  Real low = 1e-8;
  Real high = 1.0;
  for (int i = 0; i < 200 && difference(high) < 0.0; ++i) {
    high *= 2.0;
  }
  // At y+ -> 0 the log branch diverges to -inf, so difference(low) > 0. The
  // crossing we want is the UPPER one, where conduction overtakes the log law
  // from below; below that the log branch is above conduction.
  // Re-bracket accordingly: find where difference changes sign going up.
  low = high / 2.0;
  for (int i = 0; i < 200; ++i) {
    const Real middle = 0.5 * (low + high);
    if (!(middle > low) || !(middle < high)) break;
    if (difference(middle) < 0.0) {
      low = middle;
    } else {
      high = middle;
    }
  }
  return 0.5 * (low + high);
}

Real temperaturePlus(Real yPlus, Real prandtl, Real turbulentPrandtl,
                     const cfd::turbulence::WallFunctionCoefficients& coefficients) {
  constexpr const char* kContext = "temperaturePlus";
  requireNonNegative(yPlus, "yPlus", kContext);
  requirePositive(prandtl, "prandtl", kContext);
  requirePositive(turbulentPrandtl, "turbulentPrandtl", kContext);

  const Real crossing = thermalSublayerThickness(prandtl, turbulentPrandtl, coefficients);
  if (yPlus <= crossing) {
    // Conduction sublayer. Exact at y+ = 0, where T+ = 0.
    return prandtl * yPlus;
  }
  return logBranch(yPlus, turbulentPrandtl, jayatillekeP(prandtl, turbulentPrandtl), coefficients);
}

Real wallHeatTransferConductance(Real density, Real specificHeat, Real frictionVelocity, Real yPlus,
                                 Real prandtl, Real turbulentPrandtl,
                                 const cfd::turbulence::WallFunctionCoefficients& coefficients) {
  constexpr const char* kContext = "wallHeatTransferConductance";
  requirePositive(density, "density", kContext);
  requirePositive(specificHeat, "specificHeat", kContext);
  requireNonNegative(frictionVelocity, "frictionVelocity", kContext);
  requireNonNegative(yPlus, "yPlus", kContext);
  requirePositive(prandtl, "prandtl", kContext);
  requirePositive(turbulentPrandtl, "turbulentPrandtl", kContext);

  // No friction velocity means no turbulent wall transport. Returned as an
  // exact zero rather than an epsilon; the caller decides whether to fall back
  // on the molecular path.
  if (frictionVelocity == 0.0 || yPlus == 0.0) return 0.0;

  const Real tPlus = temperaturePlus(yPlus, prandtl, turbulentPrandtl, coefficients);
  if (!(tPlus > 0.0)) {
    throw InvalidArgumentError(
        "wallHeatTransferConductance: T+ is not > 0, so the wall law does not describe this "
        "state");
  }
  return density * specificHeat * frictionVelocity / tPlus;
}

ThermalWallFaceResult computeThermalWallFace(
    Real density, Real specificHeat, Real frictionVelocity, Real yPlus, Real wallArea,
    Real cellTemperature, Real wallTemperature, Real molecularPrandtl, Real turbulentPrandtl,
    const cfd::turbulence::WallFunctionCoefficients& coefficients) {
  constexpr const char* kContext = "computeThermalWallFace";
  requirePositive(wallArea, "wallArea", kContext);

  ThermalWallFaceResult result;
  result.yPlus = yPlus;
  // wallHeatTransferConductance validates everything else and returns exactly
  // zero for a stagnant or unresolved face.
  result.heatTransferCoefficient =
      wallHeatTransferConductance(density, specificHeat, frictionVelocity, yPlus, molecularPrandtl,
                                  turbulentPrandtl, coefficients);
  if (result.heatTransferCoefficient == 0.0) {
    // Leave T+ at zero too: there is no wall law here to report.
    return result;
  }
  result.temperaturePlus = temperaturePlus(yPlus, molecularPrandtl, turbulentPrandtl, coefficients);
  result.conductance = result.heatTransferCoefficient * wallArea;
  result.heatFlux = result.conductance * (wallTemperature - cellTemperature);
  return result;
}

}  // namespace cfd::thermal
