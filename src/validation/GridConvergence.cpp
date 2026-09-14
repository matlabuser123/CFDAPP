#include "cfd/validation/GridConvergence.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::validation {

namespace {

// log(expm1(x)) for x > 0, stable for small x (expm1) and large x (no
// overflow).
Real logExpm1(Real x) {
  if (x > 30.0) return x + std::log1p(-std::exp(-x));
  return std::log(std::expm1(x));
}

// F(p) = ln(r21^p (r32^p - 1) / (r21^p - 1)) - ln(ratio); strictly
// increasing in p > 0 (derivative b/(1 - e^{-bp}) - a/(e^{ap} - 1) > 0 with
// a = ln r21, b = ln r32, since x/(1 - e^{-x}) > 1 > x/(e^x - 1)).
Real orderFunction(Real p, Real a, Real b, Real logRatio) {
  return (p * a) + logExpm1(p * b) - logExpm1(p * a) - logRatio;
}

std::string format(Real value) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.6g", value);
  return buffer;
}

bool finite(Real value) { return std::isfinite(value); }

}  // namespace

Real representativeGridSize(Real measure, Index cells, int dimension) {
  if (!std::isfinite(measure) || !(measure > 0.0)) {
    throw InvalidArgumentError("representativeGridSize: measure must be finite and > 0");
  }
  if (cells == 0) {
    throw InvalidArgumentError("representativeGridSize: cells must be > 0");
  }
  if (dimension < 1 || dimension > 3) {
    throw InvalidArgumentError("representativeGridSize: dimension must be 1, 2 or 3");
  }
  return std::pow(measure / static_cast<Real>(cells), 1.0 / static_cast<Real>(dimension));
}

std::string_view convergenceClassName(ConvergenceClass value) noexcept {
  switch (value) {
    case ConvergenceClass::Monotonic:
      return "monotonic";
    case ConvergenceClass::Oscillatory:
      return "oscillatory";
    case ConvergenceClass::Divergent:
      return "divergent";
    case ConvergenceClass::InsufficientSeparation:
      return "insufficient_separation";
    case ConvergenceClass::Invalid:
      return "invalid";
  }
  return "invalid";
}

std::string_view gridConvergenceStatusName(GridConvergenceStatus value) noexcept {
  switch (value) {
    case GridConvergenceStatus::Asymptotic:
      return "asymptotic";
    case GridConvergenceStatus::MonotonicNotAsymptotic:
      return "monotonic_not_asymptotic";
    case GridConvergenceStatus::MonotonicAsymptoticRangeUnknown:
      return "monotonic_asymptotic_range_unknown";
    case GridConvergenceStatus::Oscillatory:
      return "oscillatory";
    case GridConvergenceStatus::Divergent:
      return "divergent";
    case GridConvergenceStatus::InsufficientSeparation:
      return "insufficient_separation";
    case GridConvergenceStatus::Invalid:
      return "invalid";
  }
  return "invalid";
}

ObservedOrderSolution solveObservedOrder(Real r21, Real r32, Real ratio, Real maxOrder) {
  ObservedOrderSolution solution;
  if (!finite(r21) || !finite(r32) || !(r21 > 1.0) || !(r32 > 1.0) || !finite(ratio) ||
      !(ratio > 0.0) || !finite(maxOrder) || !(maxOrder > 0.0)) {
    return solution;
  }
  const Real a = std::log(r21);
  const Real b = std::log(r32);
  const Real logRatio = std::log(ratio);
  // p -> 0+ limit of F: ln(b / a) - ln(ratio). F increasing, so a positive
  // root exists iff this limit is negative.
  if (!(std::log(b / a) - logRatio < 0.0)) return solution;
  if (orderFunction(maxOrder, a, b, logRatio) < 0.0) {
    solution.exceedsMaximum = true;
    return solution;
  }
  Real lo = 0.0;
  Real hi = maxOrder;
  Index iterations = 0;
  while (iterations < 200 && (hi - lo) > 1e-14 * std::max<Real>(1.0, hi)) {
    const Real mid = 0.5 * (lo + hi);
    ++iterations;
    // F(0) is the limit above (negative), so lo = 0 is a valid bracket end.
    if (orderFunction(mid, a, b, logRatio) < 0.0) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  solution.order = 0.5 * (lo + hi);
  solution.iterations = iterations;
  return solution;
}

GridConvergenceResult analyzeGridConvergence(const GridLevel& fine, const GridLevel& medium,
                                             const GridLevel& coarse,
                                             const GridConvergenceOptions& options) {
  GridConvergenceResult result;
  const auto invalid = [&](const std::string& why) {
    result.status = GridConvergenceStatus::Invalid;
    result.convergence = ConvergenceClass::Invalid;
    result.diagnostic = why;
    result.gridIndependenceReason = "analysis invalid: " + why;
    return result;
  };

  // --- Input validation -----------------------------------------------------
  if (!finite(fine.h) || !finite(medium.h) || !finite(coarse.h)) {
    return invalid("non-finite grid size");
  }
  if (!finite(fine.value) || !finite(medium.value) || !finite(coarse.value)) {
    return invalid("non-finite quantity value");
  }
  if (!(fine.h > 0.0) || !(medium.h > 0.0) || !(coarse.h > 0.0)) {
    return invalid("grid size h must be > 0");
  }
  if (!(fine.h < medium.h) || !(medium.h < coarse.h)) {
    return invalid("grids must be ordered h1 (fine) < h2 (medium) < h3 (coarse); got h1=" +
                   format(fine.h) + ", h2=" + format(medium.h) + ", h3=" + format(coarse.h));
  }
  if (!finite(options.safetyFactor) || !(options.safetyFactor >= 1.0)) {
    return invalid("safety factor must be finite and >= 1");
  }
  if (!finite(options.minimumRefinementRatio) || !(options.minimumRefinementRatio > 1.0)) {
    return invalid("minimumRefinementRatio must be finite and > 1");
  }
  if (!finite(options.maximumOrder) || !(options.maximumOrder > 0.0)) {
    return invalid("maximumOrder must be finite and > 0");
  }
  if (!finite(options.absoluteNoise) || options.absoluteNoise < 0.0 ||
      !finite(options.relativeNoise) || options.relativeNoise < 0.0) {
    return invalid("noise levels must be finite and >= 0");
  }
  if (!finite(options.asymptoticTolerance) || !(options.asymptoticTolerance > 0.0)) {
    return invalid("asymptoticTolerance must be finite and > 0");
  }
  if (options.formalOrder.has_value() &&
      (!finite(*options.formalOrder) || !(*options.formalOrder > 0.0))) {
    return invalid("formal order must be finite and > 0");
  }
  if (options.gridIndependenceThreshold.has_value() &&
      (!finite(*options.gridIndependenceThreshold) ||
       !(*options.gridIndependenceThreshold > 0.0))) {
    return invalid("grid-independence threshold must be finite and > 0");
  }

  result.r21 = medium.h / fine.h;
  result.r32 = coarse.h / medium.h;
  if (result.r21 < options.minimumRefinementRatio || result.r32 < options.minimumRefinementRatio) {
    return invalid("refinement ratio below " + format(options.minimumRefinementRatio) +
                   " (nearly identical grids): r21=" + format(result.r21) +
                   ", r32=" + format(result.r32));
  }
  if (result.r21 < 1.3 || result.r32 < 1.3) {
    result.warnings.push_back("refinement ratio below 1.3 (Celik et al. recommend r > 1.3)");
  }

  const Real phi1 = fine.value;
  const Real phi2 = medium.value;
  const Real phi3 = coarse.value;
  result.epsilon21 = phi2 - phi1;
  result.epsilon32 = phi3 - phi2;
  const Real scale = std::max({std::abs(phi1), std::abs(phi2), std::abs(phi3)});
  const Real noise = std::max(options.absoluteNoise, options.relativeNoise * scale);
  const bool zero21 = std::abs(result.epsilon21) <= noise;
  const bool zero32 = std::abs(result.epsilon32) <= noise;
  if (!zero32) result.convergenceRatio = result.epsilon21 / result.epsilon32;

  const auto relativeAvailable = [&](Real denominator) {
    return std::abs(denominator) > noise && std::abs(denominator) > 0.0;
  };
  const auto noGridIndependence = [&](const std::string& why) {
    result.gridIndependent = false;
    result.gridIndependenceReason = why;
  };

  // --- Classification --------------------------------------------------------
  if (zero21 && zero32) {
    result.status = GridConvergenceStatus::InsufficientSeparation;
    result.convergence = ConvergenceClass::InsufficientSeparation;
    result.diagnostic = "all three values agree within the noise level " + format(noise) +
                        ": the order and GCI are undefined";
    noGridIndependence("insufficient separation: " + result.diagnostic);
    return result;
  }
  if (zero21) {
    result.status = GridConvergenceStatus::InsufficientSeparation;
    result.convergence = ConvergenceClass::InsufficientSeparation;
    result.diagnostic = "fine/medium difference |epsilon21|=" + format(std::abs(result.epsilon21)) +
                        " is within the noise level " + format(noise) +
                        " while the medium/coarse difference is not: apparent order unbounded";
    noGridIndependence("insufficient separation: " + result.diagnostic);
    return result;
  }
  if (zero32) {
    result.status = GridConvergenceStatus::Divergent;
    result.convergence = ConvergenceClass::Divergent;
    result.diagnostic =
        "medium/coarse difference is within the noise level but the fine/medium "
        "difference is not: the differences grow with refinement";
    noGridIndependence("divergent sequence: " + result.diagnostic);
    return result;
  }
  if ((result.epsilon21 > 0.0) != (result.epsilon32 > 0.0)) {
    result.status = GridConvergenceStatus::Oscillatory;
    result.convergence = ConvergenceClass::Oscillatory;
    const Real high = std::max({phi1, phi2, phi3});
    const Real low = std::min({phi1, phi2, phi3});
    result.oscillationUncertainty = 0.5 * (high - low);
    if (relativeAvailable(phi1)) {
      result.relativeOscillationUncertainty = *result.oscillationUncertainty / std::abs(phi1);
    }
    result.diagnostic =
        "epsilon21 and epsilon32 have opposite signs (R = " + format(*result.convergenceRatio) +
        "): oscillatory convergence -- no observed order or Richardson "
        "extrapolation is reported";
    noGridIndependence("oscillatory sequence: only the oscillation bound 0.5(max-min) = " +
                       format(*result.oscillationUncertainty) + " is available");
    return result;
  }

  // Monotonic candidate: epsilon32 / epsilon21 > 0.
  const Real ratio = result.epsilon32 / result.epsilon21;
  const ObservedOrderSolution order =
      solveObservedOrder(result.r21, result.r32, ratio, options.maximumOrder);
  result.orderIterations = order.iterations;
  if (order.exceedsMaximum) {
    result.status = GridConvergenceStatus::InsufficientSeparation;
    result.convergence = ConvergenceClass::InsufficientSeparation;
    result.diagnostic = "apparent order exceeds maximumOrder " + format(options.maximumOrder) +
                        " (epsilon21 negligible against epsilon32): not trusted";
    noGridIndependence("insufficient separation: " + result.diagnostic);
    return result;
  }
  if (!order.order.has_value()) {
    result.status = GridConvergenceStatus::Divergent;
    result.convergence = ConvergenceClass::Divergent;
    result.diagnostic =
        "monotonic but the differences do not shrink fast enough for a positive "
        "order (epsilon32/epsilon21 = " +
        format(ratio) +
        " <= ln r32 / ln r21 = " + format(std::log(result.r32) / std::log(result.r21)) + ")";
    noGridIndependence("divergent sequence: " + result.diagnostic);
    return result;
  }

  const Real p = *order.order;
  result.convergence = ConvergenceClass::Monotonic;
  result.observedOrder = p;
  const Real r21p = std::pow(result.r21, p);
  const Real r32p = std::pow(result.r32, p);
  result.extrapolated21 = phi1 - (result.epsilon21 / (r21p - 1.0));
  result.extrapolated32 = phi2 - (result.epsilon32 / (r32p - 1.0));
  result.uncertainty21 = options.safetyFactor * std::abs(result.epsilon21) / (r21p - 1.0);
  result.uncertainty32 = options.safetyFactor * std::abs(result.epsilon32) / (r32p - 1.0);
  if (relativeAvailable(phi1)) {
    result.approximateRelativeError21 = std::abs(result.epsilon21 / phi1);
    result.gci21 = *result.uncertainty21 / std::abs(phi1);
  }
  if (relativeAvailable(phi2)) {
    result.approximateRelativeError32 = std::abs(result.epsilon32 / phi2);
    result.gci32 = *result.uncertainty32 / std::abs(phi2);
  }
  if (relativeAvailable(*result.extrapolated21)) {
    result.extrapolatedRelativeError21 =
        std::abs((*result.extrapolated21 - phi1) / *result.extrapolated21);
  }
  if (relativeAvailable(*result.extrapolated32)) {
    result.extrapolatedRelativeError32 =
        std::abs((*result.extrapolated32 - phi2) / *result.extrapolated32);
  }

  if (options.formalOrder.has_value()) {
    const Real pf = *options.formalOrder;
    const Real r21f = std::pow(result.r21, pf);
    const Real r32f = std::pow(result.r32, pf);
    const Real u21 = std::abs(result.epsilon21) / (r21f - 1.0);
    const Real u32 = std::abs(result.epsilon32) / (r32f - 1.0);
    result.asymptoticRatio = u32 / (r21f * u21);
    result.orderRatio = p / pf;
    const bool inBand = std::abs(*result.asymptoticRatio - 1.0) <= options.asymptoticTolerance;
    result.status =
        inBand ? GridConvergenceStatus::Asymptotic : GridConvergenceStatus::MonotonicNotAsymptotic;
    result.diagnostic = "monotonic convergence, observed order " + format(p) + " (formal " +
                        format(pf) + "); asymptotic ratio " + format(*result.asymptoticRatio) +
                        (inBand ? " within " : " outside ") + "1 +/- " +
                        format(options.asymptoticTolerance);
  } else {
    result.status = GridConvergenceStatus::MonotonicAsymptoticRangeUnknown;
    result.diagnostic = "monotonic convergence, observed order " + format(p) +
                        "; no formal order given, so the asymptotic range is not assessed";
  }

  // --- Grid independence -----------------------------------------------------
  if (!options.gridIndependenceThreshold.has_value()) {
    noGridIndependence("no grid-independence threshold configured");
  } else if (result.status != GridConvergenceStatus::Asymptotic) {
    noGridIndependence(std::string("status is ") +
                       std::string(gridConvergenceStatusName(result.status)) + ", not asymptotic");
  } else if (!result.gci21.has_value()) {
    noGridIndependence("GCI21 undefined (fine-grid value within the noise level of zero)");
  } else if (*result.gci21 <= *options.gridIndependenceThreshold) {
    result.gridIndependent = true;
    result.gridIndependenceReason = "asymptotic and GCI21 " + format(*result.gci21) +
                                    " <= threshold " + format(*options.gridIndependenceThreshold);
  } else {
    noGridIndependence("GCI21 " + format(*result.gci21) + " > threshold " +
                       format(*options.gridIndependenceThreshold));
  }
  return result;
}

}  // namespace cfd::validation
