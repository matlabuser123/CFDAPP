#pragma once

#include <optional>
#include <string>
#include <vector>

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

// P6-PHYS-001: one entry of physics.json's optional "species" array --
// identity + constant diffusivity + initial concentration for one
// transported, passive, non-reacting scalar species, mirroring
// `cfd::species::SpeciesProperties` exactly (diffusivity may be 0, "pure
// advection intentionally supported" -- see SpeciesProperties.hpp's own
// header comment; this codebase does not enforce 0<=Y<=1 or sum(Y)=1 at
// this layer either, same reasoning). `name` also doubles as the key
// boundaries.json's own per-patch "species" object must supply an entry
// for (PhysicsConfigParser.cpp/BoundaryConfigParser.cpp's own
// cross-check), and as the exported field/column name
// (concentration_<name> in fields.csv/solution.vtk, "name" in
// metadata.json's "species" array).
struct SpeciesConfig {
  std::string name;
  Real diffusivity{};
  Real initialConcentration{};
};

// P6-PHYS-002: one phase of physics.json's optional "multiphase" block,
// mirroring cfd::multiphase::PhaseProperties exactly.
struct PhasePhysicsConfig {
  std::string name;
  Real density{};
  Real viscosity{};
};

// P6-PHYS-002: physics.json's optional "multiphase" object -- same
// enable-by-presence convention as thermal/turbulence/buoyancy above.
// Exactly two phases (cfd::multiphase::TwoPhaseSystem's own mandatory
// two-phase scope -- see MultiphaseProperties.hpp's own header comment),
// never a configurable N. initialAlpha is phase1's uniform initial
// volume fraction (alpha=1 -> pure phase1, alpha=0 -> pure phase2, this
// codebase's own established convention). transportTimeStep is the
// single dt `cfd::multiphase::VolumeFractionSolver::step()` needs -- see
// VolumeFractionSolver.hpp's own header comment on why this foundation
// is a single implicit-Euler step, not an outer-iterated solve like
// thermal/species: there is no case-file "dt" anywhere else in this
// codebase (ProjectRunner is steady-SIMPLE-only), so this is the one
// place a transient parameter enters an otherwise-steady case.
struct MultiphasePhysicsConfig {
  PhasePhysicsConfig phase1;
  PhasePhysicsConfig phase2;
  Real initialAlpha{};
  Real transportTimeStep{};
};

// P6-PHYS-003: physics.json's optional "compressible" object -- same
// enable-by-presence convention as the blocks above. By default (absent
// or `"coupled": false`), this is a *post-hoc low-Mach reinterpretation*
// of an already-converged incompressible SIMPLE result (exactly
// tests/integration/compressible/test_low_mach_regression.cpp's own
// validated recipe: solve incompressible SIMPLE with physics.json's
// existing top-level density/dynamic_viscosity, then evaluate absolute
// pressure/EOS density/Mach number/compressible mass flux/continuity
// imbalance from that converged result) -- never a second, parallel flow
// solve. referencePressure converts SIMPLE's own gauge pressure to
// absolute (p_abs = referencePressure + p_gauge) for the EOS. Exactly
// one of temperature (isothermal -- test_low_mach_regression.cpp's own
// validated mode) or thermalCoupled (use the case's own "thermal" block
// converged temperature field instead of a constant) must be set --
// PhysicsConfigParser.cpp enforces this, and enforces "thermal" is
// actually enabled when thermalCoupled is requested.
//
// P12-COMP-002: `coupled: true` dispatches to
// cfd::compressible::CompressibleSIMPLE instead -- a genuinely coupled
// compressible pressure-velocity solve (density is iterated state, not a
// post-hoc read) rather than a reinterpretation of a separate
// incompressible solve. Defaults to `false` so every existing
// compressible case (including `cases/compressible_validation`) keeps
// today's exact post-hoc behavior, byte-identical, unless a case opts in
// explicitly -- see ProjectRunner.cpp's own dispatch comment for why the
// two modes are not layerable (once CompressibleSIMPLE solves for
// density/pressure natively, "reinterpretation of an already-converged
// incompressible result" no longer describes it).
struct CompressiblePhysicsConfig {
  Real gasConstant{};
  Real specificHeatPressure{};
  Real referencePressure{};
  std::optional<Real> temperature;
  bool thermalCoupled{false};
  bool coupled{false};
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
  // P6-PHYS-001: physics.json's optional "species" array. Unlike
  // thermal/turbulence/buoyancy above (each a single optional object),
  // species is inherently a *list* of independent named fields (this
  // codebase's own field-per-species convention -- see
  // SpeciesProperties.hpp's own header comment), so an empty vector
  // (rather than std::optional<vector>) already represents "no species
  // configured" without a second wrapper -- both an absent "species" key
  // and a present-but-empty "species": [] array parse to the same empty
  // vector (PhysicsConfigParser.cpp accepts both; there is no meaningful
  // difference between them worth rejecting).
  std::vector<SpeciesConfig> species;
  // P10-APP-004: see PhysicsConfigParser.cpp's validatePhysicsCompatibility
  // (the one authoritative compatibility matrix) for exactly which
  // combinations of thermal/turbulence/buoyancy/species/multiphase/
  // compressible are supported, required, or mutually exclusive.
  std::optional<MultiphasePhysicsConfig> multiphase;
  std::optional<CompressiblePhysicsConfig> compressible;
};

}  // namespace cfd::io
