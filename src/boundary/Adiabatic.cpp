#include "cfd/boundary/Adiabatic.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

BoundaryConditionType Adiabatic::type() const noexcept { return BoundaryConditionType::Adiabatic; }
std::string_view Adiabatic::name() const noexcept { return "Adiabatic"; }

Real Adiabatic::boundaryValue(Real ownerValue, Real normalDistance) const {
  if (!std::isfinite(normalDistance) || !(normalDistance > 0.0)) {
    throw InvalidArgumentError("Adiabatic: normalDistance must be finite and positive");
  }
  return ownerValue;
}

}  // namespace cfd::boundary
