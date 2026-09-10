#pragma once

#include <optional>
#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io {

// P2-THERMAL-004: physics.json's optional "thermal" object. Presence of
// this object IS the enable flag -- there is deliberately no separate
// "enabled" boolean (an absent block already means "thermal disabled",
// matching InitialConditions's own "defaults to zero when absent"
// precedent; a present-but-disabled block would be a redundant third
// state with no clear meaning).
struct ThermalPhysicsConfig {
  Real conductivity{};
  Real specificHeat{};
  Real initialTemperature{};
};

// P2-TURB-004 (extended by P2-TURB-005 and P2-TURB-006): physics.json's
// optional "turbulence" object -- same enable-by-presence convention as
// ThermalPhysicsConfig above (an absent block means laminar; there is no
// separate "enabled" boolean). `model` is the case-facing selector
// string CaseBuilder passes to turbulence::createTurbulenceModel()
// ("laminar", "k_epsilon", "k_omega", or "sst"). initialK is shared by
// all three real models (k_epsilon, k_omega, and sst all transport a k
// equation); exactly one of initialEpsilon/initialOmega is populated --
// initialOmega for both "k_omega" and "sst" (sst transports omega, not
// epsilon), initialEpsilon only for "k_epsilon" -- and
// PhysicsConfigParser.cpp enforces "the field for the *other* model must
// be absent" so a case cannot supply both or neither. kRelaxation and
// exactly one of epsilonRelaxation/omegaRelaxation are optional: absent
// means the relevant KEpsilonConfig/KOmegaConfig/SSTConfig's own default
// (0.7); the inner BiCGSTAB solves reuse SolverConfig::momentumSolver's
// own tolerances, not a separate pair -- see PhysicsConfigParser.cpp's
// own comment on why this task does not expose a k/epsilon-, k/omega-,
// or sst-specific linear-solver-tolerance pair in physics.json.
struct TurbulencePhysicsConfig {
  std::string model;
  Real initialK{};
  std::optional<Real> initialEpsilon;
  std::optional<Real> initialOmega;
  std::optional<Real> kRelaxation;
  std::optional<Real> epsilonRelaxation;
  std::optional<Real> omegaRelaxation;
};

// P3-PHYS-001: physics.json's optional "buoyancy" object -- same
// enable-by-presence convention as ThermalPhysicsConfig/
// TurbulencePhysicsConfig above. `model` is always "boussinesq" for now
// (the only buoyancy model this codebase implements -- TODO.md's own
// "do not implement compressible density coupling" constraint rules out
// a second model existing yet). Requires "thermal" to also be present
// (PhysicsConfigParser.cpp's own cross-block check): a buoyancy source
// needs a temperature field to evaluate against, and this codebase has
// no other source of one. referenceDensity is deliberately *not* a
// field here -- same reasoning as ThermalPhysicsConfig not duplicating
// density (FluidProperties::density(), from this same physics.json's
// top-level "density" field, is single-sourced and passed to
// BoussinesqBuoyancy's constructor by CaseBuilder).
struct BuoyancyPhysicsConfig {
  Real beta{};
  Real referenceTemperature{};
  Vector2 gravity{};
};

// model is always "incompressible_laminar" for the current numerical
// scope. reynoldsNumber is optional reporting-only metadata (TODO.md P1
// section 10) -- it is never used to derive density/viscosity; those two
// stay the explicit physical inputs.
struct PhysicsConfig {
  std::string model;
  Real density{};
  Real dynamicViscosity{};
  std::optional<Real> reynoldsNumber;
  std::optional<ThermalPhysicsConfig> thermal;
  std::optional<TurbulencePhysicsConfig> turbulence;
  std::optional<BuoyancyPhysicsConfig> buoyancy;
};

}  // namespace cfd::io
