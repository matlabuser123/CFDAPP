#include "cfd/thermal/TemperatureDependentProperty.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::thermal {

using cfd::fields::ScalarField;

namespace {

// Horner, highest coefficient down. The order is part of the contract -- see
// the header -- because a different summation order gives different bits.
Real horner(const std::vector<Real>& coefficients, Real temperature) {
  Real result = coefficients.back();
  for (std::size_t i = coefficients.size() - 1; i-- > 0;) {
    result = result * temperature + coefficients[i];
  }
  return result;
}

}  // namespace

TemperatureDependentProperty::TemperatureDependentProperty(std::vector<Real> coefficients,
                                                           Real minimumTemperature,
                                                           Real maximumTemperature,
                                                           std::string name)
    : coefficients_(std::move(coefficients)),
      minimumTemperature_(minimumTemperature),
      maximumTemperature_(maximumTemperature),
      name_(std::move(name)) {
  if (coefficients_.empty()) {
    throw InvalidArgumentError("TemperatureDependentProperty(" + name_ +
                               "): at least one coefficient is required");
  }
  for (std::size_t i = 0; i < coefficients_.size(); ++i) {
    if (!std::isfinite(coefficients_[i])) {
      throw InvalidArgumentError("TemperatureDependentProperty(" + name_ + "): coefficient " +
                                 std::to_string(i) + " is not finite");
    }
  }
  if (!std::isfinite(minimumTemperature_) || !std::isfinite(maximumTemperature_)) {
    throw InvalidArgumentError("TemperatureDependentProperty(" + name_ +
                               "): the validity range must be finite");
  }
  if (!(maximumTemperature_ > minimumTemperature_)) {
    throw InvalidArgumentError("TemperatureDependentProperty(" + name_ +
                               "): maximumTemperature must exceed minimumTemperature");
  }

  // Positivity is checked by SAMPLING the range, not by root-finding: a
  // polynomial of arbitrary degree has no closed-form minimum, and a sampled
  // check with a fine grid catches every case a fitted material property
  // realistically produces. Sampling can in principle miss a narrow dip
  // between samples -- stated rather than hidden -- so the sample count is
  // deliberately large relative to any physically meaningful property curve.
  constexpr int kSamples = 1024;
  for (int s = 0; s <= kSamples; ++s) {
    const Real t = minimumTemperature_ + (maximumTemperature_ - minimumTemperature_) *
                                             (static_cast<Real>(s) / static_cast<Real>(kSamples));
    const Real value = horner(coefficients_, t);
    if (!std::isfinite(value) || !(value > 0.0)) {
      throw InvalidArgumentError("TemperatureDependentProperty(" + name_ +
                                 "): value must be finite and > 0 over the whole validity range; "
                                 "at T = " +
                                 std::to_string(t) + " it is " + std::to_string(value));
    }
  }
}

TemperatureDependentProperty TemperatureDependentProperty::constant(Real value, std::string name) {
  // Deliberately the SAME class, not a special case: one evaluation path means
  // the constant result cannot drift from the varying one.
  return TemperatureDependentProperty({value}, 0.0, 1.0e6, std::move(name));
}

Real TemperatureDependentProperty::evaluate(Real temperature) const {
  if (!std::isfinite(temperature)) {
    throw InvalidArgumentError("TemperatureDependentProperty(" + name_ +
                               "): temperature is not finite");
  }
  if (temperature < minimumTemperature_ || temperature > maximumTemperature_) {
    throw InvalidArgumentError(
        "TemperatureDependentProperty(" + name_ + "): temperature " + std::to_string(temperature) +
        " is outside the validity range [" + std::to_string(minimumTemperature_) + ", " +
        std::to_string(maximumTemperature_) +
        "]. Extrapolating a fitted property is refused rather than silently performed.");
  }
  return horner(coefficients_, temperature);
}

Real TemperatureDependentProperty::value(Real temperature) const { return evaluate(temperature); }

ScalarField TemperatureDependentProperty::evaluateField(const ScalarField& temperature) const {
  ScalarField result(temperature.size(), 0.0);
  for (Index i = 0; i < temperature.size(); ++i) {
    const Real t = temperature[i];
    if (!std::isfinite(t) || t < minimumTemperature_ || t > maximumTemperature_) {
      // Names the cell and the value: a diagnostic the user can act on, the
      // convention P13-002 established.
      throw InvalidArgumentError(
          "TemperatureDependentProperty(" + name_ + "): cell " + std::to_string(i) +
          " has temperature " + std::to_string(t) + ", outside the validity range [" +
          std::to_string(minimumTemperature_) + ", " + std::to_string(maximumTemperature_) + "]");
    }
    result[i] = horner(coefficients_, t);
  }
  return result;
}

}  // namespace cfd::thermal
