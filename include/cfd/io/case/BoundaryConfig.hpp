#pragma once

#include <map>
#include <optional>
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

// P2-THERMAL-004: temperature's thermal-specific BC types
// (boundary::FixedTemperature/HeatFlux/Adiabatic -- P2-THERMAL-003).
// "value" is the prescribed temperature for "fixed_temperature", the
// heat flux q'' for "heat_flux" (see boundary::HeatFlux's own sign-
// convention header comment), and unused/must-not-be-present for
// "adiabatic" -- same "only some types take a value" shape as
// VelocityBoundarySpec above, not PressureBoundarySpec's always-required
// value (adiabatic structurally has none, unlike either pressure type).
struct TemperatureBoundarySpec {
  std::string type;
  Real value{};
};

// P6-PHYS-001: one species's concentration BC on one patch. Species
// transport is a plain passive scalar with no structurally-valueless
// type the way temperature's "adiabatic" is (see
// cfd::species::SpeciesEquation.hpp's own header comment: only
// FixedValue/FixedGradient are needed), so this has exactly
// PressureBoundarySpec's shape (both types always take a "value") rather
// than TemperatureBoundarySpec's "only some types take a value" one --
// "fixed_gradient" with value 0.0 already expresses a zero-flux
// impermeable wall, with no need for a separate no-value type.
struct ConcentrationBoundarySpec {
  std::string type;
  Real value{};
};

// P6-PHYS-002: phase-1 volume-fraction BC on one patch -- pure advection
// (VolumeFractionEquation.hpp's own explicit "no diffusion term"), so
// same always-required-value fixed_value/fixed_gradient shape as
// ConcentrationBoundarySpec above (no structurally-valueless type
// needed).
struct AlphaBoundarySpec {
  std::string type;
  Real value{};
};

struct PatchBoundaryConfig {
  VelocityBoundarySpec velocity;
  PressureBoundarySpec pressure;
  // Present iff physics.json configured a "thermal" block -- CaseReader
  // enforces this per patch during parsing (P2-THERMAL-004), not as a
  // separate cross-file check.
  std::optional<TemperatureBoundarySpec> temperature;
  // P6-PHYS-001: keyed by species name, one entry per physics.json-
  // declared species -- empty iff no species are configured. CaseReader
  // enforces "exactly the declared species-name set, on every patch"
  // during parsing (BoundaryConfigParser.cpp), the same "per-patch key
  // required/forbidden based on physics.json" convention
  // temperature/thermalEnabled already established, generalized from one
  // boolean flag to a set of required keys.
  std::map<std::string, ConcentrationBoundarySpec, std::less<>> concentration;
  // P6-PHYS-002: present iff physics.json configured a "multiphase"
  // block -- same convention as `temperature` above.
  std::optional<AlphaBoundarySpec> alpha;
};

// Keyed by patch name ("left"/"right"/"bottom"/"top" for the only
// supported geometry/mesh combination). std::less<> (transparent
// comparator) lets callers look up by string_view without constructing a
// temporary std::string.
struct BoundaryConfig {
  std::map<std::string, PatchBoundaryConfig, std::less<>> patches;
};

}  // namespace cfd::io
