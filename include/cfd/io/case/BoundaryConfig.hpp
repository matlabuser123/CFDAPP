#pragma once

#include <map>
#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io {

// Velocity and pressure need separate BC configuration per patch
// (TODO.md P1 section 13-14): they are physically different fields with
// different supported BC types, not one BC object applied identically to
// both. Only the BC types that already have a concrete VectorBoundary
// Condition class are supported for velocity -- "wall", "moving_wall",
// "inlet", "outlet", "symmetry". There is deliberately no vector
// "fixed_value"/"fixed_gradient" velocity type: this codebase's boundary
// module expresses those cases as Inlet(velocity) and Outlet()
// respectively (see cfd/boundary/Outlet.hpp), not as generic vector BCs.
struct VelocityBoundarySpec {
  std::string type;
  // Only meaningful for "moving_wall" and "inlet" -- ignored (and must
  // not be present in the source JSON) for every other type.
  Vector2 value{};
};

// Pressure is scalar, so both of this codebase's ScalarBoundaryCondition
// classes are exposed directly -- "fixed_value" (Dirichlet) and
// "fixed_gradient" (Neumann, the usual choice paired with Wall/
// MovingWall/Inlet/Symmetry velocity patches -- see
// PressureCorrectionEquation.hpp's own boundary-treatment doc comment).
struct PressureBoundarySpec {
  std::string type;
  Real value{};
};

struct PatchBoundaryConfig {
  VelocityBoundarySpec velocity;
  PressureBoundarySpec pressure;
};

// Keyed by patch name ("left"/"right"/"bottom"/"top" for the only
// supported geometry/mesh combination). std::less<> (transparent
// comparator) lets callers look up by string_view without constructing a
// temporary std::string.
struct BoundaryConfig {
  std::map<std::string, PatchBoundaryConfig, std::less<>> patches;
};

}  // namespace cfd::io
