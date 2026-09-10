#include "cfd/boundary/WallOmega.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

WallOmega::WallOmega(Real kinematicViscosity, Real beta1)
    : kinematicViscosity_(kinematicViscosity), beta1_(beta1) {
  if (!std::isfinite(kinematicViscosity_) || !(kinematicViscosity_ > 0.0)) {
    throw InvalidArgumentError("WallOmega: kinematicViscosity must be finite and > 0");
  }
  if (!std::isfinite(beta1_) || !(beta1_ > 0.0)) {
    throw InvalidArgumentError("WallOmega: beta1 must be finite and > 0");
  }
}

Real WallOmega::kinematicViscosity() const noexcept { return kinematicViscosity_; }
Real WallOmega::beta1() const noexcept { return beta1_; }

BoundaryConditionType WallOmega::type() const noexcept { return BoundaryConditionType::WallOmega; }
std::string_view WallOmega::name() const noexcept { return "WallOmega"; }

Real WallOmega::boundaryValue(Real /*ownerValue*/, Real normalDistance) const {
  if (!std::isfinite(normalDistance) || !(normalDistance > 0.0)) {
    throw InvalidArgumentError("WallOmega: normalDistance must be finite and positive");
  }
  // omega_wall = 60*nu / (beta1*y^2) -- see this class's own header
  // comment for the derivation/interpretation.
  return (60.0 * kinematicViscosity_) / (beta1_ * normalDistance * normalDistance);
}

}  // namespace cfd::boundary
