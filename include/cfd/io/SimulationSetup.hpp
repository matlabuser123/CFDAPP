#pragma once

#include <optional>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/multiphase/MultiphaseProperties.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/species/SpeciesProperties.hpp"
#include "cfd/thermal/ThermalProperties.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"

namespace cfd::io {

// P6-PHYS-001: one runtime-ready species transport setup -- everything
// `cfd::species::SpeciesSolver::solve()` needs for exactly one species
// (see SpeciesSolver.hpp's own header comment: one instance transports
// one species per call, multiple independent species means one
// SpeciesSetup per species, never a dedicated multi-species solver
// class), mirroring SimulationSetup's own "already the actual runtime
// types the solver takes" convention.
struct SpeciesSetup {
  cfd::species::SpeciesProperties properties;
  cfd::fields::ScalarField initialConcentration;
  cfd::boundary::BoundaryConditionSet concentrationBoundaries;
};

// P6-PHYS-002: everything `cfd::multiphase::VolumeFractionSolver::step()`
// plus the mixture-property field evaluators need. `transportTimeStep` is
// the single dt `step()` requires (VolumeFractionSolver.hpp's own
// "single implicit-Euler step, no outer loop" scope). Momentum's own
// mixture-viscosity coupling (see ProjectRunner.cpp's own header
// comment) is built from `system`/`initialAlpha` by the caller, not
// stored here -- same "CaseBuilder builds data, the caller
// builds/orchestrates the solve" split every other optional physics
// block in this struct already follows.
struct MultiphaseSetup {
  cfd::multiphase::TwoPhaseSystem system;
  cfd::fields::ScalarField initialAlpha;
  cfd::boundary::BoundaryConditionSet alphaBoundaries;
  Real transportTimeStep{};
};

// P6-PHYS-003: everything the post-hoc low-Mach reinterpretation pass
// (see CompressiblePhysicsConfig's own header comment for why this is
// post-hoc, not a genuine compressible solve) needs. Exactly one of
// `temperature` (isothermal) or `thermalCoupled` is meaningful --
// `thermalCoupled` true means "use the case's own converged thermal
// field instead", the same convention CompressiblePhysicsConfig itself
// uses.
struct CompressibleSetup {
  cfd::compressible::ThermodynamicProperties thermodynamics;
  Real referencePressure{};
  std::optional<Real> temperature;
  bool thermalCoupled{false};
};

// The bridge between configuration and CFD execution (TODO.md P1 section
// 24): everything a solver needs, already built as the actual runtime
// types SIMPLE::solve() takes -- not a pre-constructed SIMPLE itself
// (section 25: CaseReader/CaseBuilder must not hand back an
// already-running solver). The reference cell (pressure null-space gauge)
// is deliberately not part of this -- it stays the deterministic default
// 0 a caller passes directly to SIMPLE's constructor (section 43).
//
// thermal/temperatureBoundaries/initialTemperature (P2-THERMAL-004) are
// all std::optional together, present iff physics.json configured a
// "thermal" block -- CaseBuilder either sets all three or leaves all
// three empty, never a mix (CaseReader's own per-patch "temperature key
// required iff thermal enabled" validation guarantees there is always
// enough configuration to build all three once thermal is enabled at
// all). Check thermal.has_value() to decide whether to run
// thermal::ThermalSolver at all -- there is no separate "thermal
// enabled" bool to keep in sync with it.
struct SimulationSetup {
  cfd::mesh::Mesh mesh;
  cfd::physics::FluidProperties fluid;
  cfd::boundary::BoundaryConditionSet velocityBoundaries;
  cfd::boundary::BoundaryConditionSet pressureBoundaries;
  cfd::pressure_velocity::SIMPLESettings solverSettings;
  cfd::fields::VectorField initialVelocity;
  cfd::fields::ScalarField initialPressure;

  std::optional<cfd::thermal::ThermalProperties> thermal;
  std::optional<cfd::boundary::BoundaryConditionSet> temperatureBoundaries;
  std::optional<cfd::fields::ScalarField> initialTemperature;

  // P3-PHYS-001: present iff physics.json configured a "buoyancy" block
  // (which itself requires "thermal" to also be present -- see
  // PhysicsConfigParser.cpp's own cross-block check). Unlike the
  // turbulence configs below, BoussinesqBuoyancy holds no mesh/boundary
  // references (just plain reference-density/beta/referenceTemperature/
  // gravity values), so CaseBuilder constructs the real object directly
  // here rather than leaving that to the caller -- there is no memory-
  // safety trap to avoid the way there is for a TurbulenceModel. A
  // caller passes `&setup.initialTemperature.value()` (or its own
  // live-updated temperature field, once one exists) and
  // `&setup.buoyancy.value()` straight into `SIMPLE`'s own optional
  // temperature/buoyancy constructor pair. **Not yet wired into
  // `apps/cli/main.cpp`'s one-shot `runCase()` orchestration** -- a
  // real coupled natural-convection CLI run needs an outer Picard loop
  // alternating `SIMPLE`/`cfd::thermal::ThermalSolver` (see
  // `cfd::pressure_velocity::SIMPLE`'s own header comment and
  // `tests/integration/thermal/test_boussinesq_coupling.cpp` for a
  // worked example of that loop), which is a deliberate, documented
  // scope decision for this task, not an oversight -- same "foundation
  // now, CLI integration later" precedent P2-THERMAL-005 already
  // established for conjugate heat transfer.
  std::optional<cfd::physics::BoussinesqBuoyancy> buoyancy;

  // P2-TURB-004 (extended by P2-TURB-005 and P2-TURB-006): at most one of
  // kEpsilonConfig/kOmegaConfig/sstConfig is present, matching
  // physics.json's "turbulence.model" ("k_epsilon", "k_omega", or "sst"
  // respectively); all three are absent for laminar (an absent
  // "turbulence" block and an explicit "model": "laminar" both collapse
  // to this same all-nullopt state, since both mean "SIMPLE/PISO use
  // their own null-turbulenceModel default", which is already exactly
  // laminar -- TODO.md P2-TURB-003).
  //
  // Deliberately data only, NOT a constructed
  // cfd::turbulence::TurbulenceModel -- this struct's own header comment
  // above ("not a pre-constructed SIMPLE itself") applies equally to a
  // turbulence model: it is solver-facing runtime state, not
  // configuration, so CaseBuilder does not construct one. A caller
  // builds `cfd::turbulence::KEpsilonModel(setup.mesh, setup.fluid,
  // setup.velocityBoundaries, *setup.kBoundaries, *setup.epsilonBoundaries,
  // *setup.kEpsilonConfig)` (or the KOmegaModel/kOmegaConfig/
  // omegaBoundaries, or SSTModel/sstConfig/omegaBoundaries, equivalent)
  // itself, exactly the same "CaseBuilder builds data, the caller builds
  // SIMPLE" split already used for solverSettings/SIMPLE. This also
  // sidesteps a real memory-safety trap a stored TurbulenceModel would
  // create: KEpsilonModel/KOmegaModel/SSTModel hold mesh/boundary-set
  // members *by reference*, so they can only safely reference fields
  // that have already reached their final, stable address -- never a
  // field of the same SimulationSetup that might still be relocated by
  // its own return-by-value construction.
  //
  // kBoundaries is populated whenever *any* config is present (all three
  // models transport a k equation against the same derived boundary
  // set); epsilonBoundaries is populated only alongside kEpsilonConfig.
  // omegaBoundaries is shared by kOmegaConfig and sstConfig (both
  // transport an omega equation) but built differently per model: for
  // "k_omega" it is a zero-gradient wall approximation (KOmegaModel's
  // own documented simplification -- see CaseBuilder.cpp); for "sst" it
  // is the real Wilcox near-wall omega Dirichlet value via
  // cfd::boundary::WallOmega, since SST already computes a real wall
  // distance field and KOmegaModel's simplification is not silently
  // reused (see WallOmega.hpp's own header comment).
  std::optional<cfd::turbulence::KEpsilonConfig> kEpsilonConfig;
  std::optional<cfd::turbulence::KOmegaConfig> kOmegaConfig;
  std::optional<cfd::turbulence::SSTConfig> sstConfig;
  std::optional<cfd::boundary::BoundaryConditionSet> kBoundaries;
  std::optional<cfd::boundary::BoundaryConditionSet> epsilonBoundaries;
  std::optional<cfd::boundary::BoundaryConditionSet> omegaBoundaries;

  // P6-PHYS-001: one entry per physics.json-declared species, in
  // declaration order -- runtime-ready to hand straight to
  // `cfd::species::SpeciesSolver::solve()` alongside a converged
  // `massFlux` (see SpeciesSolver.hpp's own header comment: one
  // `SpeciesSolver` instance transports exactly one species per call, so
  // a caller loops over this vector calling `solve()` once per entry,
  // never a dedicated multi-species solver class). Empty iff physics.json
  // configured no species -- there is no separate "species enabled" bool
  // to keep in sync with it, same convention as PhysicsConfig::species
  // itself (see PhysicsConfig.hpp's own header comment).
  std::vector<SpeciesSetup> species;

  // P6-PHYS-002/003: present iff physics.json configured the
  // corresponding block -- same convention as thermal/buoyancy above.
  // At most one of multiphase/turbulence is ever populated together (see
  // PhysicsConfigParser.cpp's own cross-block rejection).
  std::optional<MultiphaseSetup> multiphase;
  std::optional<CompressibleSetup> compressible;
};

}  // namespace cfd::io
