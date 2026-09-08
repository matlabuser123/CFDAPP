#include "cfd/boundary/Symmetry.hpp"

#include <cmath>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

BoundaryConditionType Symmetry::type() const noexcept { return BoundaryConditionType::Symmetry; }
std::string_view Symmetry::name() const noexcept { return "Symmetry"; }

Vector2 Symmetry::boundaryValue(const Vector2& ownerValue, Real /*normalDistance*/,
                                const Vector2& unitNormal) const {
  const Real normalMagnitude = magnitude(unitNormal);
  if (!std::isfinite(normalMagnitude) || std::abs(normalMagnitude - 1.0) > constants::small) {
    throw InvalidArgumentError("Symmetry: unitNormal must have unit magnitude");
  }
  const Real normalComponent = dot(ownerValue, unitNormal);
  return ownerValue - (unitNormal * normalComponent);
}

}  // namespace cfd::boundary
