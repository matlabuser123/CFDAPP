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
// P15: a temperature-dependent property as configured in physics.json.
// Mirrors cfd::thermal::TemperatureDependentProperty, which validates it
// again at construction -- two independent layers, the same deliberate
// redundancy this file's other parsers already use.
struct TemperaturePolynomialConfig {
  std::vector<Real> coefficients;  // ascending: a0, a1, a2, ...
  Real minimumTemperature{};
  Real maximumTemperature{};
};

struct ThermalPhysicsConfig {
  // Always populated. When the corresponding model below is present, this
  // holds the model evaluated at initialTemperature, so a consumer that does
  // not know about models still receives a valid in-range value rather than a
  // zero. Consumers that do know read the model instead.
  Real conductivity{};
  Real specificHeat{};
  Real initialTemperature{};

  // Present only when physics.json gave the object form.
  std::optional<TemperaturePolynomialConfig> conductivityModel;
  std::optional<TemperaturePolynomialConfig> specificHeatModel;
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

  // P16: high-Re wall functions for the k-epsilon model.
  //
  // Absent means OFF, which is the pre-P16 behaviour byte for byte -- every
  // existing case keeps the near-wall treatment it had. A case opts in with
  // "wall_functions": true.
  //
  // k_epsilon ONLY for now. k_omega and SST have their own near-wall
  // treatments and their wall-function work is deliberately not part of this
  // step, so asking for it there is rejected rather than silently ignored.
  std::optional<bool> wallFunctions;

  // P16b: WHICH near-wall treatment, when wallFunctions is on.
  // "high_re" (the default when absent, so every existing case is
  // unchanged) or "automatic" -- Menter blended treatment, which stays
  // defined at tau_w = 0 and is therefore the only one of the two usable
  // in separated flow. SST only: the blend sublayer omega branch is built
  // on SST own beta1, and no equivalent is implemented for the other
  // models, so asking for it there is rejected rather than ignored.
  std::optional<std::string> wallTreatment;

  // P16: the turbulent Prandtl number, Pr_t, closing the turbulent heat flux
  // by the Reynolds analogy:  k_t = mu_t c_p / Pr_t.
  //
  // It lives in the TURBULENCE block, not the thermal one, and deliberately:
  // Pr_t means nothing without a mu_t to scale, so a laminar case carrying one
  // is dead configuration. Putting it here makes that rejection structural
  // rather than a cross-block check -- the same reasoning that keeps
  // wall_functions here and refuses it on a model that has none.
  //
  // Absent means kDefaultTurbulentPrandtl. The default is resolved HERE, in the
  // configuration, and the thermal solver receives a resolved value: a solver
  // that supplied its own default could silently disagree with the configured
  // one.
  std::optional<Real> turbulentPrandtl;
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

// ===================================================================
// G8 -- physics.json's optional "reacting" object.
//
// WHY A SEPARATE BLOCK FROM "species" ABOVE. P6-PHYS-001's "species" array drives
// `cfd::species::SpeciesSolver`: independent, passive, non-reacting scalars with no
// `sum(Y)=1`, no bounds enforcement and no mixture thermodynamics. The P18 stack is a
// different thing -- `MassFractionState` (bounds + `sum(Y)=1`, REFUSING rather than
// normalising), `MixtureThermodynamics`, `ReactionMechanism`, `ArrheniusKinetics`,
// `HeatRelease` -- and it was never reachable from a case file.
//
// Extending "species" in place would have meant bolting conditionals onto a working
// production path and reproving every passive-species case bitwise. CLAUDE.md section 6
// prefers a dedicated new path, so this is one: "reacting" is parsed, built and
// dispatched separately, and "species" is untouched. A case may use either, never both
// (PhysicsConfigParser rejects the combination) -- two species representations in one
// solve would be two sources of truth for the same field.
//
// ENABLE BY PRESENCE, like thermal/turbulence/buoyancy/multiphase/compressible.
// ===================================================================

// One species of the reacting mixture. Mirrors what `cfd::species::SpeciesRegistry` plus
// `cfd::species::SpeciesThermodynamics` plus `cfd::species::FormationEnthalpy` need, so a
// reader can see the whole per-species input in one place.
//
// ON molecular_weight: these must be MUTUALLY CONSISTENT, not independently rounded table
// values. G6 requires `sum_i M_i nu_i = 0` to `4*eps*max|M_i nu_i|` for every reaction, and
// that is a statement about the weights. Independently rounded values miss it by ~1e-6
// kg/mol -- about 1.8e10 times the bound, measured in
// tests/unit/species/test_reaction_orders.cpp. Derive each weight from one atomic mass per
// element and the balance closes exactly; the parser cannot check this per species, so the
// MECHANISM-level closure check is where a wrong set is caught.
struct ReactingSpeciesConfig {
  std::string name;
  Real molecularWeight{};      // kg/mol, element-derived (see above)
  Real specificHeat{};         // cp, J/(kg K), constant
  Real formationEnthalpy{};    // h_f, J/kg, any sign
  Real initialMassFraction{};  // Y_i at t=0; the set must sum to 1
  Real diffusivity{};          // m^2/s, may be 0
};

// One side's entry, or one reaction order. Both are "a species name and a number", but they
// are DIFFERENT numbers and keeping one struct for both would invite writing an order into a
// coefficient -- which destroys `sum_i M_i nu_i = 0`. Named distinctly for that reason.
struct ReactionTermConfig {
  std::string species;
  Real coefficient{};
};
struct ReactionOrderConfig {
  std::string species;
  Real order{};
};

// One reaction: topology, optional explicit orders, and its Arrhenius parameters.
//
// `orders` is OPTIONAL. Omit it and the orders are the reactant stoichiometry, which is mass
// action. Supply it for a fitted GLOBAL mechanism whose exponents are not its stoichiometry --
// Westbrook & Dryer (1981) Table II Set 3 for methane is `CH4 + 2 O2 -> CO2 + 2 H2O` with
// exponents 0.2 and 1.3. A NEGATIVE order is refused: the rate would diverge as that species
// is consumed and the remedy is a concentration floor this codebase forbids.
//
// Arrhenius is `A * T^n * exp(-Ea/(R_u T))` with Ea in J/mol -- MOLE based, never kmol. A
// kmol activation energy is wrong by exactly 1000 and is the error
// `ArrheniusTest.N4_AKmolGasConstantIsWrongByOrdersOfMagnitude` exists to catch.
struct ReactingReactionConfig {
  std::string name;
  std::vector<ReactionTermConfig> reactants;
  std::vector<ReactionTermConfig> products;
  std::vector<ReactionOrderConfig> orders;  // empty => mass action
  bool reversible{false};
  Real preExponential{};       // A
  Real temperatureExponent{};  // n
  Real activationEnergy{};     // Ea, J/mol
};

// physics.json's optional "reacting" object.
struct ReactingPhysicsConfig {
  std::vector<ReactingSpeciesConfig> species;
  std::vector<ReactingReactionConfig> reactions;
  Real initialTemperature{};  // K, the unburnt state

  // TD-001 / amendment A1: the single dt the reacting advance takes, s.
  //
  // WHY A TIME STEP IS REQUIRED RATHER THAN A STEADY PASS. A steady solve with the reaction source
  // frozen at the initial state has NO source treatment that is both conservative and bounded, and
  // that was measured both ways: splitting the source Patankar-style keeps every Y_i positive but
  // loses sum(Y_i) = 1 by 0.300, because consumption self-limits while production runs at the
  // initial rate; treating it fully explicitly conserves to the solver tolerance but drives Y_A to
  // -0.297. The asymmetry is structural, not a tuning problem.
  //
  // An operator-split TIME STEP has neither failure: the transport half carries no reaction at all
  // (so its operator is an M-matrix and every Y_i stays bounded) and the chemistry half applies
  // sum_i omega_i = 0 locally and consistently (so mass is conserved by construction).
  //
  // The precedent is multiphase's `transport_time_step`, which exists for the same reason: a single
  // implicit-Euler advance needs the caller to say how far.
  Real timeStep{};

  // TD-002: how many operator-split steps the march takes. Straight from physics.json's
  // reacting.steps.
  //
  // REQUIRED, not defaulted, for the same reason `time_step` is: the integration EXTENT decides the
  // reported composition, so a silently chosen value would make two runs of the "same" case differ
  // by a build. `steps: 1` is the TD-001 single advance and reproduces its figures bitwise -- which
  // is gate criterion C11, measured rather than asserted.
  //
  // dt and steps rather than an end time: the march is first-order Lie splitting, so the answer
  // depends on dt itself and not only on dt*steps. Two numbers that each mean one thing beat one
  // number that hides the step size.
  Index steps{};
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

  // G8: the REACTING mixture, a separate path from `species` above. A case may declare one or
  // the other, never both.
  std::optional<ReactingPhysicsConfig> reacting;
  // P10-APP-004: see PhysicsConfigParser.cpp's validatePhysicsCompatibility
  // (the one authoritative compatibility matrix) for exactly which
  // combinations of thermal/turbulence/buoyancy/species/multiphase/
  // compressible are supported, required, or mutually exclusive.
  std::optional<MultiphasePhysicsConfig> multiphase;
  std::optional<CompressiblePhysicsConfig> compressible;
};

}  // namespace cfd::io
