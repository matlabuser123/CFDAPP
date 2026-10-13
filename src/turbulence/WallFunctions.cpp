#include "cfd/turbulence/WallFunctions.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::turbulence {

namespace {

void requireFinite(Real value, const char* what) {
  if (!std::isfinite(value)) {
    throw InvalidArgumentError(std::string("WallFunctions: ") + what + " must be finite");
  }
}

void requirePositive(Real value, const char* what) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("WallFunctions: ") + what + " must be finite and > 0");
  }
}

void requireNonNegative(Real value, const char* what) {
  if (!std::isfinite(value) || value < 0.0) {
    throw InvalidArgumentError(std::string("WallFunctions: ") + what + " must be finite and >= 0");
  }
}

void requireCoefficients(const WallFunctionCoefficients& c) {
  requirePositive(c.kappa, "kappa");
  requirePositive(c.E, "E");
  requirePositive(c.cMu, "cMu");
}

}  // namespace

Real logLawIntercept(const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  return std::log(coefficients.E) / coefficients.kappa;
}

// --- friction velocity -------------------------------------------------------

Real frictionVelocityFromWallShear(Real wallShearStress, Real density) {
  requireNonNegative(wallShearStress, "wallShearStress");
  requirePositive(density, "density");
  return std::sqrt(wallShearStress / density);
}

Real frictionVelocityFromTurbulentEnergy(Real turbulentKineticEnergy, Real cMu) {
  requireNonNegative(turbulentKineticEnergy, "turbulentKineticEnergy");
  requirePositive(cMu, "cMu");
  // C_mu^(1/4) * sqrt(k) == (cMu * k * k)^(1/4) would be one rounding shorter,
  // but the two-step form is what every reference writes and what the
  // hand-checked tests are derived from. Keeping the textbook spelling means a
  // reader comparing against a paper is comparing the same expression.
  return std::pow(cMu, 0.25) * std::sqrt(turbulentKineticEnergy);
}

// --- wall coordinates --------------------------------------------------------

Real yPlus(Real density, Real frictionVelocity, Real wallDistance, Real dynamicViscosity) {
  requirePositive(density, "density");
  requireNonNegative(frictionVelocity, "frictionVelocity");
  requireNonNegative(wallDistance, "wallDistance");
  requirePositive(dynamicViscosity, "dynamicViscosity");
  return density * frictionVelocity * wallDistance / dynamicViscosity;
}

Real uPlus(Real velocityMagnitude, Real frictionVelocity) {
  requireFinite(velocityMagnitude, "velocityMagnitude");
  requirePositive(frictionVelocity, "frictionVelocity");
  return velocityMagnitude / frictionVelocity;
}

// --- the two near-wall laws --------------------------------------------------

Real uPlusViscousSublayer(Real yPlusValue) {
  requireNonNegative(yPlusValue, "yPlus");
  return yPlusValue;
}

Real uPlusLogLaw(Real yPlusValue, const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requirePositive(yPlusValue, "yPlus");
  return (std::log(yPlusValue) / coefficients.kappa) + logLawIntercept(coefficients);
}

Real viscousLogLayerIntersection(const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  // f(y+) = y+ - (ln(y+)/kappa + B) is negative at small y+ (where the log law
  // over-predicts) and positive well beyond the crossing, and is monotonic
  // across that bracket, so bisection converges to the single root.
  const auto f = [&](Real y) { return y - uPlusLogLaw(y, coefficients); };

  Real low = 1.0;
  Real high = 100.0;
  if (f(low) > 0.0 || f(high) < 0.0) {
    throw InvalidArgumentError(
        "viscousLogLayerIntersection: the viscous and log laws do not cross in [1, 100] for "
        "these coefficients -- check that kappa and E are a physically meaningful pair");
  }
  // 200 halvings takes a bracket of 99 far below 1e-12; the loop is bounded so
  // a pathological coefficient pair cannot spin here.
  for (int iteration = 0; iteration < 200 && (high - low) > 1e-12; ++iteration) {
    const Real mid = 0.5 * (low + high);
    if (f(mid) < 0.0) {
      low = mid;
    } else {
      high = mid;
    }
  }
  return 0.5 * (low + high);
}

// --- wall resolution ---------------------------------------------------------

WallResolution classifyWallResolution(Real yPlusValue) {
  requireNonNegative(yPlusValue, "yPlus");
  if (yPlusValue <= 5.0) return WallResolution::ViscousSublayer;
  if (yPlusValue < kWallFunctionMinimumYPlus) return WallResolution::BufferLayer;
  if (yPlusValue <= kWallFunctionMaximumYPlus) return WallResolution::LogLayer;
  return WallResolution::TooCoarse;
}

std::string_view wallResolutionName(WallResolution resolution) {
  switch (resolution) {
    case WallResolution::ViscousSublayer:
      return "ViscousSublayer";
    case WallResolution::BufferLayer:
      return "BufferLayer";
    case WallResolution::LogLayer:
      return "LogLayer";
    case WallResolution::TooCoarse:
      return "TooCoarse";
  }
  return "Unknown";
}

bool isWallFunctionResolutionValid(Real yPlusValue) {
  return classifyWallResolution(yPlusValue) == WallResolution::LogLayer;
}

// --- the Launder-Spalding wall function --------------------------------------

Real wallShearStressLaunderSpalding(Real velocityMagnitude, Real turbulentKineticEnergy,
                                    Real wallDistance, Real density, Real dynamicViscosity,
                                    const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireFinite(velocityMagnitude, "velocityMagnitude");
  requireNonNegative(turbulentKineticEnergy, "turbulentKineticEnergy");
  requirePositive(wallDistance, "wallDistance");
  requirePositive(density, "density");
  requirePositive(dynamicViscosity, "dynamicViscosity");

  // No velocity, no shear. Returned rather than divided through, because the
  // limit is exact and not an approximation worth hiding behind an epsilon.
  if (velocityMagnitude == 0.0) return 0.0;

  const Real uTauStar =
      frictionVelocityFromTurbulentEnergy(turbulentKineticEnergy, coefficients.cMu);
  const Real yStar = yPlus(density, uTauStar, wallDistance, dynamicViscosity);
  if (!(yStar > 0.0)) {
    throw InvalidArgumentError(
        "wallShearStressLaunderSpalding: y* is not > 0, so ln(E y*) is undefined -- the "
        "inputs do not describe a turbulent wall cell (k is zero, or the wall distance "
        "vanished)");
  }
  const Real denominator = std::log(coefficients.E * yStar);
  if (!(denominator > 0.0)) {
    throw InvalidArgumentError(
        "wallShearStressLaunderSpalding: ln(E y*) is not > 0, which puts this cell below the "
        "log law's own validity -- at y* this small the high-Re wall function does not apply");
  }
  return density * uTauStar * coefficients.kappa * velocityMagnitude / denominator;
}

Real wallAdjacentDissipation(Real turbulentKineticEnergy, Real wallDistance,
                             const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireNonNegative(turbulentKineticEnergy, "turbulentKineticEnergy");
  requirePositive(wallDistance, "wallDistance");
  const Real uTauStar =
      frictionVelocityFromTurbulentEnergy(turbulentKineticEnergy, coefficients.cMu);
  return uTauStar * uTauStar * uTauStar / (coefficients.kappa * wallDistance);
}

Real wallAdjacentProduction(Real wallShearStress, Real frictionVelocityFromK, Real wallDistance,
                            const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireNonNegative(wallShearStress, "wallShearStress");
  requireNonNegative(frictionVelocityFromK, "frictionVelocityFromK");
  requirePositive(wallDistance, "wallDistance");
  return wallShearStress * frictionVelocityFromK / (coefficients.kappa * wallDistance);
}

Real wallAdjacentSpecificDissipation(Real turbulentKineticEnergy, Real wallDistance,
                                     const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireNonNegative(turbulentKineticEnergy, "turbulentKineticEnergy");
  requirePositive(wallDistance, "wallDistance");
  const Real uTauStar =
      frictionVelocityFromTurbulentEnergy(turbulentKineticEnergy, coefficients.cMu);
  return uTauStar / (std::sqrt(coefficients.cMu) * coefficients.kappa * wallDistance);
}

Real wallAdjacentTurbulentEnergy(Real frictionVelocity,
                                 const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireNonNegative(frictionVelocity, "frictionVelocity");
  return frictionVelocity * frictionVelocity / std::sqrt(coefficients.cMu);
}

// --- the effective wall viscosity --------------------------------------------

Real effectiveWallViscosity(Real wallShearStress, Real velocityMagnitude, Real wallDistance,
                            Real dynamicViscosity, bool* floored) {
  requireNonNegative(wallShearStress, "wallShearStress");
  requireFinite(velocityMagnitude, "velocityMagnitude");
  requirePositive(wallDistance, "wallDistance");
  requirePositive(dynamicViscosity, "dynamicViscosity");
  if (floored != nullptr) *floored = false;

  // No velocity means no shear to match; the molecular value is the limit.
  if (velocityMagnitude == 0.0) return dynamicViscosity;

  const Real muWall = wallShearStress * wallDistance / std::abs(velocityMagnitude);
  if (muWall < dynamicViscosity) {
    // mu_t >= 0. See the header: this is the positivity constraint, not a
    // convenience clamp, and it is reported rather than applied silently.
    if (floored != nullptr) *floored = true;
    return dynamicViscosity;
  }
  return muWall;
}

// --- P16b: the automatic (blended) near-wall treatment ----------------------

std::string_view wallTreatmentModeName(WallTreatmentMode mode) {
  switch (mode) {
    case WallTreatmentMode::HighReWallFunction:
      return "HighReWallFunction";
    case WallTreatmentMode::Automatic:
      return "Automatic";
    case WallTreatmentMode::AutomaticSpalding:
      return "AutomaticSpalding";
    case WallTreatmentMode::AutomaticMomentumFlux:
      return "AutomaticMomentumFlux";
  }
  return "Unknown";
}

Real viscousSublayerSpecificDissipation(Real kinematicViscosity, Real beta1, Real wallDistance) {
  requirePositive(kinematicViscosity, "kinematicViscosity");
  requirePositive(beta1, "beta1");
  requirePositive(wallDistance, "wallDistance");
  // 6 nu / (beta1 y^2) -- the sublayer solution at the cell centre. See the
  // header on why this is 6 and cfd::boundary::WallOmega is 60.
  return 6.0 * kinematicViscosity / (beta1 * wallDistance * wallDistance);
}

Real viscousFrictionVelocity(Real velocityMagnitude, Real wallDistance, Real kinematicViscosity) {
  requireFinite(velocityMagnitude, "velocityMagnitude");
  requirePositive(wallDistance, "wallDistance");
  requirePositive(kinematicViscosity, "kinematicViscosity");
  // sqrt(nu |U| / y). Zero at U = 0 rather than undefined -- that is the whole
  // point at a separation or reattachment point.
  return std::sqrt(kinematicViscosity * std::abs(velocityMagnitude) / wallDistance);
}

Real blendFrictionVelocity(Real viscous, Real logarithmic) {
  requireNonNegative(viscous, "viscous friction velocity");
  requireNonNegative(logarithmic, "logarithmic friction velocity");
  const Real v2 = viscous * viscous;
  const Real l2 = logarithmic * logarithmic;
  // (v^4 + l^4)^(1/4), formed as sqrt(sqrt(v^4 + l^4)) via the squares so the
  // intermediate cannot overflow for large arguments the way pow(x, 4) can.
  return std::sqrt(std::sqrt((v2 * v2) + (l2 * l2)));
}

Real blendSpecificDissipation(Real viscous, Real logarithmic) {
  requireNonNegative(viscous, "viscous omega");
  requireNonNegative(logarithmic, "logarithmic omega");
  return std::sqrt((viscous * viscous) + (logarithmic * logarithmic));
}

WallRegime classifyWallRegime(Real yPlusValue) {
  requireNonNegative(yPlusValue, "yPlus");
  if (yPlusValue <= 5.0) return WallRegime::ViscousSublayer;
  if (yPlusValue < kWallFunctionMinimumYPlus) return WallRegime::Blended;
  return WallRegime::LogLayer;
}

std::string_view wallRegimeName(WallRegime regime) {
  switch (regime) {
    case WallRegime::ViscousSublayer:
      return "ViscousSublayer";
    case WallRegime::Blended:
      return "Blended";
    case WallRegime::LogLayer:
      return "LogLayer";
  }
  return "Unknown";
}

AutomaticWallState automaticWallState(Real velocityMagnitude, Real turbulentKineticEnergy,
                                      Real wallDistance, Real density, Real dynamicViscosity,
                                      Real beta1, const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireFinite(velocityMagnitude, "velocityMagnitude");
  requireNonNegative(turbulentKineticEnergy, "turbulentKineticEnergy");
  requirePositive(wallDistance, "wallDistance");
  requirePositive(density, "density");
  requirePositive(dynamicViscosity, "dynamicViscosity");
  requirePositive(beta1, "beta1");

  const Real nu = dynamicViscosity / density;

  AutomaticWallState state;
  state.viscousFrictionVelocity = viscousFrictionVelocity(velocityMagnitude, wallDistance, nu);
  // u_tau* -- the equilibrium scale. omega_log and the equilibrium
  // production are built on this, and both are part of the validated path.
  state.equilibriumFrictionVelocity =
      frictionVelocityFromTurbulentEnergy(turbulentKineticEnergy, coefficients.cMu);

  // The LOG BRANCH of the wall-shear blend is the Launder-Spalding friction
  // velocity, so that when the viscous branch vanishes this reduces EXACTLY
  // to the wall shear the high-Re path computes:
  //
  //   tau_LS = rho kappa u_tau* |U| / ln(E y*) = rho u_tau* |U| / u+
  //   u_tau,log = sqrt(tau_LS / rho)
  //
  // An earlier version used u_tau* itself here. That is a DIFFERENT
  // quantity -- the two coincide only at exact equilibrium -- and it made
  // the automatic mode disagree with the validated path by 25% on the
  // Re_tau = 590 channel at y+ = 69, where the blend itself contributes
  // under 1%.
  //
  // u+ is floored at 1 so this stays defined where ln(E y*) <= 0. The
  // high-Re path THROWS there, correctly, because it has no validity below
  // y* = 0.154; the automatic path must not, because that is the separated
  // state it exists for. Below that y* the viscous branch is larger by
  // orders of magnitude, so the floor cannot change a blended answer.
  if (state.equilibriumFrictionVelocity > 0.0 && velocityMagnitude != 0.0) {
    const Real yStar =
        yPlus(density, state.equilibriumFrictionVelocity, wallDistance, dynamicViscosity);
    const Real uPlusLogarithmic =
        yStar > 0.0 ? std::max(uPlusLogLaw(yStar, coefficients), 1.0) : 1.0;
    state.logarithmicFrictionVelocity = std::sqrt(state.equilibriumFrictionVelocity *
                                                  std::abs(velocityMagnitude) / uPlusLogarithmic);
  } else {
    // No turbulence or no velocity means no log-layer shear. Exact, not
    // an epsilon.
    state.logarithmicFrictionVelocity = 0.0;
  }
  state.frictionVelocity =
      blendFrictionVelocity(state.viscousFrictionVelocity, state.logarithmicFrictionVelocity);

  state.wallShearStress = density * state.frictionVelocity * state.frictionVelocity;
  state.yPlus = yPlus(density, state.frictionVelocity, wallDistance, dynamicViscosity);
  state.regime = classifyWallRegime(state.yPlus);

  state.viscousSpecificDissipation = viscousSublayerSpecificDissipation(nu, beta1, wallDistance);
  state.logarithmicSpecificDissipation =
      wallAdjacentSpecificDissipation(turbulentKineticEnergy, wallDistance, coefficients);
  state.specificDissipation = blendSpecificDissipation(state.viscousSpecificDissipation,
                                                       state.logarithmicSpecificDissipation);
  // The square-weight of that same blend. Guarded because both branches
  // can only vanish together if nu itself is zero, which requirePositive
  // has already refused -- so this is a defensive zero, not a reachable
  // fallback.
  {
    const Real viscousSquared = state.viscousSpecificDissipation * state.viscousSpecificDissipation;
    const Real logarithmicSquared =
        state.logarithmicSpecificDissipation * state.logarithmicSpecificDissipation;
    const Real total = viscousSquared + logarithmicSquared;
    state.logLayerWeight = total > 0.0 ? logarithmicSquared / total : 0.0;
  }

  // mu_wall = tau_w y / |U|, the same momentum bridge the high-Re mode uses.
  //
  // The limit at U -> 0 is exact rather than clamped here: tau_w -> rho
  // u_tau_vis^2 = mu |U| / y, so mu_wall -> mu. The molecular value is what a
  // resolved sublayer should give, so the floor that the high-Re path needs as
  // a guard is reached naturally by this branch instead of being imposed.
  if (velocityMagnitude == 0.0) {
    state.effectiveViscosity = dynamicViscosity;
  } else {
    state.effectiveViscosity = std::max(
        state.wallShearStress * wallDistance / std::abs(velocityMagnitude), dynamicViscosity);
  }
  return state;
}

// --- P16b attempt 4: Spalding's composite law -------------------------------

namespace {

// sum_{n>=5} x^n / n!  -- the bracketed tail of Spalding's law.
//
// Evaluated as an explicit series for small x rather than as
// exp(x) - 1 - x - x^2/2 - x^3/6 - x^4/24. That closed form is catastrophically
// cancelling there: at x = 0.1 it subtracts five terms summing to ~1.105 to
// leave ~8.3e-8, losing about eight significant digits. The series has no
// cancellation at all -- every term is positive.
Real spaldingSeriesTail(Real x) {
  if (x < 0.5) {
    Real term = 1.0;
    for (int n = 1; n <= 4; ++n) {
      term *= x / static_cast<Real>(n);  // now x^4/4!
    }
    Real sum = 0.0;
    for (int n = 5; n < 64; ++n) {
      term *= x / static_cast<Real>(n);  // now x^n/n!
      sum += term;
      if (term <= 1e-18 * sum) break;
    }
    return sum;
  }
  const Real x2 = x * x;
  return std::exp(x) - 1.0 - x - (0.5 * x2) - (x2 * x / 6.0) - (x2 * x2 / 24.0);
}

}  // namespace

Real spaldingYPlus(Real uPlus, const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireNonNegative(uPlus, "uPlus");
  // e^(-kappa B) = 1/E exactly, because B = ln(E)/kappa in this repository's
  // log law. Written as 1/E rather than exp(-kappa*logLawIntercept(...)) so the
  // two forms cannot drift apart if the intercept is ever expressed
  // differently.
  return uPlus + (spaldingSeriesTail(coefficients.kappa * uPlus) / coefficients.E);
}

Real spaldingFrictionVelocity(Real velocityMagnitude, Real wallDistance, Real kinematicViscosity,
                              const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireFinite(velocityMagnitude, "velocityMagnitude");
  requirePositive(wallDistance, "wallDistance");
  requirePositive(kinematicViscosity, "kinematicViscosity");

  const Real speed = std::abs(velocityMagnitude);
  // Exact, not an epsilon: at a separation or reattachment point there is no
  // wall shear, and the law says so without any special case.
  if (speed == 0.0) return 0.0;

  const Real scale = wallDistance / kinematicViscosity;
  const auto residual = [&](Real frictionVelocity) {
    const Real value = spaldingYPlus(speed / frictionVelocity, coefficients);
    // The exponential overflows for very large u+. That only happens far to the
    // low side of the root, where the residual is hugely positive anyway, so
    // reporting +inf keeps the correct sign rather than producing a NaN.
    if (!std::isfinite(value)) return std::numeric_limits<Real>::infinity();
    return value - (frictionVelocity * scale);
  };

  // Rigorous lower bound -- see the header. residual(low) >= 0 always.
  Real low = std::sqrt(kinematicViscosity * speed / wallDistance);
  Real high = low * 2.0;
  for (int i = 0; i < 4096 && residual(high) > 0.0; ++i) {
    high *= 2.0;
  }

  // Bisection to adjacent doubles. Deterministic, and it cannot return a
  // non-root: the bracket is maintained by sign at every step.
  for (int i = 0; i < 200; ++i) {
    const Real middle = 0.5 * (low + high);
    if (!(middle > low) || !(middle < high)) break;
    if (residual(middle) > 0.0) {
      low = middle;
    } else {
      high = middle;
    }
  }
  return 0.5 * (low + high);
}

AutomaticWallState compositeWallState(Real velocityMagnitude, Real turbulentKineticEnergy,
                                      Real wallDistance, Real density, Real dynamicViscosity,
                                      Real beta1, const WallFunctionCoefficients& coefficients) {
  // Everything except the shear comes from the blended state, so omega, the
  // branch classification and the log-layer weight have exactly one
  // implementation between the two modes.
  AutomaticWallState state =
      automaticWallState(velocityMagnitude, turbulentKineticEnergy, wallDistance, density,
                         dynamicViscosity, beta1, coefficients);

  const Real nu = dynamicViscosity / density;
  state.frictionVelocity =
      spaldingFrictionVelocity(velocityMagnitude, wallDistance, nu, coefficients);
  state.wallShearStress = density * state.frictionVelocity * state.frictionVelocity;
  state.yPlus = yPlus(density, state.frictionVelocity, wallDistance, dynamicViscosity);
  state.regime = classifyWallRegime(state.yPlus);

  // Same momentum bridge as every other mode. The U -> 0 limit is exact:
  // Spalding returns u_tau -> sqrt(nu|U|/y), so tau_w y/|U| -> mu.
  if (velocityMagnitude == 0.0) {
    state.effectiveViscosity = dynamicViscosity;
  } else {
    state.effectiveViscosity = std::max(
        state.wallShearStress * wallDistance / std::abs(velocityMagnitude), dynamicViscosity);
  }
  return state;
}

AutomaticMomentumWallState automaticMomentumWallState(
    Real velocityMagnitude, Real turbulentKineticEnergy, Real wallDistance, Real density,
    Real dynamicViscosity, const WallFunctionCoefficients& coefficients) {
  requireCoefficients(coefficients);
  requireFinite(velocityMagnitude, "velocityMagnitude");
  requireNonNegative(turbulentKineticEnergy, "turbulentKineticEnergy");
  requirePositive(wallDistance, "wallDistance");
  requirePositive(density, "density");
  requirePositive(dynamicViscosity, "dynamicViscosity");

  const Real nu = dynamicViscosity / density;
  const Real speed = std::abs(velocityMagnitude);

  AutomaticMomentumWallState state;
  // The viscous-gradient scale, shared by both blends: sqrt(nu |U|/y).
  state.viscousVelocityScale = std::sqrt(nu * speed / wallDistance);
  // The turbulence scale, u_tau* = C_mu^(1/4) sqrt(k). Deliberately NOT called
  // a friction velocity -- conflating it with one is the defect this attempt
  // exists to remove.
  state.turbulenceVelocityScale =
      frictionVelocityFromTurbulentEnergy(turbulentKineticEnergy, coefficients.cMu);

  // u* first: it depends on nothing below it.
  state.momentumWallVelocityScale =
      blendFrictionVelocity(state.viscousVelocityScale, state.turbulenceVelocityScale);

  // y* is formed from u*, the Launder-Spalding convention, so the high-y+ limit
  // lands on the validated path exactly.
  state.yStar =
      state.momentumWallVelocityScale > 0.0
          ? yPlus(density, state.momentumWallVelocityScale, wallDistance, dynamicViscosity)
          : 0.0;
  // u+ floored at 1 for the same reason as elsewhere: ln(E y*) goes negative
  // below y* = 0.154, where the log law has no meaning and the viscous scale
  // dominates by orders of magnitude anyway.
  const Real uPlusLogarithmic =
      state.yStar > 0.0 ? std::max(uPlusLogLaw(state.yStar, coefficients), 1.0) : 1.0;
  state.logFrictionVelocity = speed / uPlusLogarithmic;

  state.wallFrictionVelocity =
      blendFrictionVelocity(state.viscousVelocityScale, state.logFrictionVelocity);

  // THE POINT OF THIS ATTEMPT: a product of two DIFFERENT scales.
  state.wallShearStress = density * state.wallFrictionVelocity * state.momentumWallVelocityScale;

  // mu_w = tau_w y / |U|, derived in the header. At U = 0 the flux itself is
  // zero and the ratio is 0/0; the molecular value is the correct limit for a
  // resolved wall and is what the momentum assembly needs to stay well posed.
  if (speed == 0.0) {
    state.effectiveViscosity = dynamicViscosity;
  } else {
    state.effectiveViscosity =
        std::max(state.wallShearStress * wallDistance / speed, dynamicViscosity);
  }
  return state;
}

}  // namespace cfd::turbulence
