#include "cfd/thermal/ThermalProperties.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::thermal {

namespace {

void validatePositiveFinite(Real value, const char* fieldName) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("ThermalProperties: ") + fieldName +
                               " must be finite and > 0");
  }
}

}  // namespace

ThermalProperties::ThermalProperties(Real conductivity, Real specificHeat)
    : conductivity_(conductivity), specificHeat_(specificHeat) {
  validatePositiveFinite(conductivity_, "conductivity");
  validatePositiveFinite(specificHeat_, "specificHeat");
}

Real ThermalProperties::conductivity() const noexcept { return conductivity_; }
Real ThermalProperties::specificHeat() const noexcept { return specificHeat_; }

Real ThermalProperties::thermalDiffusivity(Real density) const {
  validatePositiveFinite(density, "density");
  return conductivity_ / (density * specificHeat_);
}

}  // namespace cfd::thermal
