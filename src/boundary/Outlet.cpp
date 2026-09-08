#include "cfd/boundary/Outlet.hpp"

namespace cfd::boundary {

BoundaryConditionType Outlet::type() const noexcept { return BoundaryConditionType::Outlet; }
std::string_view Outlet::name() const noexcept { return "Outlet"; }

Vector2 Outlet::boundaryValue(const Vector2& ownerValue, Real /*normalDistance*/,
                              const Vector2& /*unitNormal*/) const {
  return ownerValue;
}

}  // namespace cfd::boundary
