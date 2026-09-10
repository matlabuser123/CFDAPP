#include "cfd/physics/TemperatureProperty.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::physics {

using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

namespace {

void requireFinite(Real value, const char* name) {
  if (!std::isfinite(value)) {
    throw InvalidArgumentError(std::string("TemperatureProperty: ") + name + " must be finite");
  }
}

}  // namespace

ConstantProperty::ConstantProperty(Real referenceValue) : referenceValue_(referenceValue) {
  requireFinite(referenceValue_, "referenceValue");
}

Real ConstantProperty::value(Real temperature) const {
  requireFinite(temperature, "temperature");
  return referenceValue_;
}

Real ConstantProperty::referenceValue() const noexcept { return referenceValue_; }

LinearProperty::LinearProperty(Real referenceValue, Real referenceTemperature, Real slope)
    : referenceValue_(referenceValue), referenceTemperature_(referenceTemperature), slope_(slope) {
  requireFinite(referenceValue_, "referenceValue");
  requireFinite(referenceTemperature_, "referenceTemperature");
  requireFinite(slope_, "slope");
}

Real LinearProperty::value(Real temperature) const {
  requireFinite(temperature, "temperature");
  return referenceValue_ + slope_ * (temperature - referenceTemperature_);
}

Real LinearProperty::referenceValue() const noexcept { return referenceValue_; }
Real LinearProperty::referenceTemperature() const noexcept { return referenceTemperature_; }
Real LinearProperty::slope() const noexcept { return slope_; }

TabulatedProperty::TabulatedProperty(std::vector<Real> temperatures, std::vector<Real> values)
    : temperatures_(std::move(temperatures)), values_(std::move(values)) {
  if (temperatures_.size() < 2) {
    throw InvalidArgumentError("TabulatedProperty: need at least 2 points");
  }
  if (temperatures_.size() != values_.size()) {
    throw InvalidArgumentError("TabulatedProperty: temperatures and values must be the same size");
  }
  for (std::size_t i = 0; i < temperatures_.size(); ++i) {
    if (!std::isfinite(temperatures_[i])) {
      throw InvalidArgumentError("TabulatedProperty: every temperature must be finite");
    }
    if (!std::isfinite(values_[i])) {
      throw InvalidArgumentError("TabulatedProperty: every value must be finite");
    }
    if (i > 0 && !(temperatures_[i] > temperatures_[i - 1])) {
      throw InvalidArgumentError(
          "TabulatedProperty: temperatures must be strictly increasing (no duplicates)");
    }
  }
}

Real TabulatedProperty::value(Real temperature) const {
  requireFinite(temperature, "temperature");

  // Constant endpoint extrapolation (see this class's own header
  // comment for why).
  if (temperature <= temperatures_.front()) return values_.front();
  if (temperature >= temperatures_.back()) return values_.back();

  // Linear scan: table sizes in this codebase's own use are small
  // (a handful of nodes), so a binary search would be premature
  // optimization (P3-PHYS-003 section 24's own "do not optimize
  // prematurely") -- this also keeps the bracket-finding logic
  // trivially easy to hand-verify against the worked examples in
  // test_temperature_property.cpp.
  std::size_t hi = 1;
  while (hi < temperatures_.size() - 1 && temperatures_[hi] < temperature) ++hi;
  const std::size_t lo = hi - 1;

  const Real t0 = temperatures_[lo], t1 = temperatures_[hi];
  const Real p0 = values_[lo], p1 = values_[hi];
  const Real weight = (temperature - t0) / (t1 - t0);
  return p0 + weight * (p1 - p0);
}

Real TabulatedProperty::tableMinTemperature() const noexcept { return temperatures_.front(); }
Real TabulatedProperty::tableMaxTemperature() const noexcept { return temperatures_.back(); }

ScalarField evaluatePropertyField(const Mesh& mesh, const ScalarField& temperature,
                                  const TemperatureProperty& property, bool requirePositive) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "evaluatePropertyField: temperature size does not match mesh cell count");
  }
  ScalarField result(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    const Real p = property.value(temperature[i]);
    if (requirePositive && (!std::isfinite(p) || !(p > 0.0))) {
      throw InvalidArgumentError("evaluatePropertyField: property value at cell " +
                                 std::to_string(i) + " must be finite and > 0 (got " +
                                 std::to_string(p) + ")");
    }
    result[i] = p;
  }
  return result;
}

}  // namespace cfd::physics
