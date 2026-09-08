#pragma once

#include <optional>
#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::io {

// model is always "incompressible_laminar" for the current numerical
// scope. reynoldsNumber is optional reporting-only metadata (TODO.md P1
// section 10) -- it is never used to derive density/viscosity; those two
// stay the explicit physical inputs.
struct PhysicsConfig {
  std::string model;
  Real density{};
  Real dynamicViscosity{};
  std::optional<Real> reynoldsNumber;
};

}  // namespace cfd::io
