#include "cfd/thermal/Enthalpy.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::thermal {

namespace {

// integral_0^T of sum_i b_i theta^i  =  sum_i b_i/(i+1) * T^{i+1}
//
// Evaluated as an explicit ascending sum rather than by Horner: the
// antiderivative's coefficients b_i/(i+1) differ from the original, and writing
// the sum out is what makes the "/(i+1)" visible at the point it matters. This
// is the term the naive h = cp*T gets wrong -- for cp = b0 + b1 T the integral
// carries b1/2 T^2, the product carries b1 T^2.
Real antiderivative(const std::vector<Real>& coefficients, Real temperature) {
  Real total = 0.0;
  Real power = temperature;  // T^{i+1}, starting at i = 0
  for (std::size_t i = 0; i < coefficients.size(); ++i) {
    total += coefficients[i] / static_cast<Real>(i + 1) * power;
    power *= temperature;
  }
  return total;
}

}  // namespace

SensibleEnthalpy::SensibleEnthalpy(TemperatureDependentProperty specificHeat,
                                   Real referenceTemperature, Real referenceEnthalpy)
    : specificHeat_(std::move(specificHeat)),
      referenceTemperature_(referenceTemperature),
      referenceEnthalpy_(referenceEnthalpy) {
  if (!std::isfinite(referenceTemperature_) || !std::isfinite(referenceEnthalpy_)) {
    throw InvalidArgumentError(
        "SensibleEnthalpy: reference temperature and enthalpy must be finite");
  }
  // Evaluating at the reference temperature validates it against the property's
  // range, with that class's own diagnostic.
  (void)specificHeat_.evaluate(referenceTemperature_);
}

Real SensibleEnthalpy::enthalpy(Real temperature) const {
  // Range-checks via the property model, so an out-of-range T is refused here
  // exactly as it is everywhere else.
  (void)specificHeat_.evaluate(temperature);
  const auto& b = specificHeat_.coefficients();
  return referenceEnthalpy_ + antiderivative(b, temperature) -
         antiderivative(b, referenceTemperature_);
}

Real SensibleEnthalpy::temperature(Real enthalpy) const {
  if (!std::isfinite(enthalpy)) {
    throw InvalidArgumentError("SensibleEnthalpy::temperature: enthalpy is not finite");
  }
  Real low = specificHeat_.minimumTemperature();
  Real high = specificHeat_.maximumTemperature();
  const Real hLow = this->enthalpy(low);
  const Real hHigh = this->enthalpy(high);

  // h is strictly increasing because cp > 0 over the range is enforced by
  // TemperatureDependentProperty's constructor. So the bracket is ordered and
  // bisection is guaranteed to converge.
  if (enthalpy < hLow || enthalpy > hHigh) {
    throw InvalidArgumentError(
        "SensibleEnthalpy::temperature: enthalpy " + std::to_string(enthalpy) +
        " is outside the range [" + std::to_string(hLow) + ", " + std::to_string(hHigh) +
        "] implied by the specific-heat model's temperature range. Refused rather than clamped: a "
        "clamped temperature would not correspond to the energy that produced it.");
  }

  // Bisection. The iteration count is bounded by the bracket width and the
  // tolerance, not by an initial guess -- which is why it is used instead of
  // Newton: no derivative, no overshoot outside the validity range.
  for (int iteration = 0; iteration < 200; ++iteration) {
    const Real middle = 0.5 * (low + high);
    if (high - low <= kInversionToleranceKelvin) return middle;
    if (this->enthalpy(middle) < enthalpy) {
      low = middle;
    } else {
      high = middle;
    }
  }
  // Unreachable for any bracket a double can represent: 200 halvings reduce any
  // finite range below the tolerance. Reported rather than returned silently,
  // because reaching it would mean an assumption above is wrong.
  throw NumericalError(
      "SensibleEnthalpy::temperature: bisection did not converge; this indicates a non-monotonic "
      "enthalpy, which the positive-cp constraint should have made impossible");
}

}  // namespace cfd::thermal
