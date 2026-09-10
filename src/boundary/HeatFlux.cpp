#include "cfd/boundary/HeatFlux.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

HeatFlux::HeatFlux(Real heatFlux, Real conductivity)
    : heatFlux_(heatFlux), conductivity_(conductivity) {
  if (!std::isfinite(heatFlux_)) {
    throw InvalidArgumentError("HeatFlux: heatFlux must be finite");
  }
  if (!std::isfinite(conductivity_) || !(conductivity_ > 0.0)) {
    throw InvalidArgumentError("HeatFlux: conductivity must be finite and > 0");
  }
}

Real HeatFlux::heatFlux() const noexcept { return heatFlux_; }
Real HeatFlux::conductivity() const noexcept { return conductivity_; }

BoundaryConditionType HeatFlux::type() const noexcept { return BoundaryConditionType::HeatFlux; }
std::string_view HeatFlux::name() const noexcept { return "HeatFlux"; }

Real HeatFlux::boundaryValue(Real ownerValue, Real normalDistance) const {
  if (!std::isfinite(normalDistance) || !(normalDistance > 0.0)) {
    throw InvalidArgumentError("HeatFlux: normalDistance must be finite and positive");
  }
  const Real gradient = -heatFlux_ / conductivity_;  // dT/dn = -q''/k
  return ownerValue + (gradient * normalDistance);
}

}  // namespace cfd::boundary
