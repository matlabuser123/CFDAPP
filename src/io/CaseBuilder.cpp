#include "cfd/io/CaseBuilder.hpp"

#include <memory>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/boundary/WallOmega.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::io {

using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::FixedValue;
using cfd::boundary::HeatFlux;
using cfd::boundary::Inlet;
using cfd::boundary::MovingWall;
using cfd::boundary::Outlet;
using cfd::boundary::Symmetry;
using cfd::boundary::Wall;
using cfd::boundary::WallOmega;
using cfd::compressible::ThermodynamicProperties;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::multiphase::PhaseProperties;
using cfd::multiphase::TwoPhaseSystem;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::species::SpeciesProperties;
using cfd::thermal::ThermalProperties;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KOmegaConfig;
using cfd::turbulence::SSTConfig;

namespace {

// Both branches below only ever see a type string CaseReader's own
// parsing already restricted to the supported set -- the default clauses
// exist so a future addition to BoundaryConfig's supported-type list
// can't silently fall through here unnoticed, not because a
// CaseDefinition CaseBuilder receives is expected to carry anything else.
std::unique_ptr<cfd::boundary::VectorBoundaryCondition> buildVelocityBoundary(
    const VelocityBoundarySpec& spec) {
  if (spec.type == "wall") return std::make_unique<Wall>();
  if (spec.type == "moving_wall") return std::make_unique<MovingWall>(spec.value);
  if (spec.type == "inlet") return std::make_unique<Inlet>(spec.value);
  if (spec.type == "outlet") return std::make_unique<Outlet>();
  if (spec.type == "symmetry") return std::make_unique<Symmetry>();
  throw CaseConfigurationError("CaseBuilder: unsupported velocity boundary type \"" + spec.type +
                               "\"");
}

std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildPressureBoundary(
    const PressureBoundarySpec& spec) {
  if (spec.type == "fixed_value") return std::make_unique<FixedValue>(spec.value);
  if (spec.type == "fixed_gradient") return std::make_unique<FixedGradient>(spec.value);
  throw CaseConfigurationError("CaseBuilder: unsupported pressure boundary type \"" + spec.type +
                               "\"");
}

// P6-PHYS-001: same two types as buildPressureBoundary above (see
// ConcentrationBoundarySpec's own header comment for why).
std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildConcentrationBoundary(
    const ConcentrationBoundarySpec& spec) {
  if (spec.type == "fixed_value") return std::make_unique<FixedValue>(spec.value);
  if (spec.type == "fixed_gradient") return std::make_unique<FixedGradient>(spec.value);
  throw CaseConfigurationError("CaseBuilder: unsupported concentration boundary type \"" +
                               spec.type + "\"");
}

// P6-PHYS-002: same two types as buildPressureBoundary above (see
// AlphaBoundarySpec's own header comment for why).
std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildAlphaBoundary(
    const AlphaBoundarySpec& spec) {
  if (spec.type == "fixed_value") return std::make_unique<FixedValue>(spec.value);
  if (spec.type == "fixed_gradient") return std::make_unique<FixedGradient>(spec.value);
  throw CaseConfigurationError("CaseBuilder: unsupported alpha boundary type \"" + spec.type +
                               "\"");
}

// P2-THERMAL-004: conductivity comes from the case's single
// ThermalProperties (not from the boundary spec itself) -- a HeatFlux
// condition needs it to convert q'' into dT/dn (see HeatFlux.hpp's own
// header comment on why it takes conductivity as a constructor argument
// rather than reading a live ThermalProperties), and this keeps that
// value single-sourced from physics.json's one "thermal.conductivity"
// field rather than letting a case duplicate it per boundary patch.
std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildTemperatureBoundary(
    const TemperatureBoundarySpec& spec, Real conductivity) {
  if (spec.type == "fixed_temperature") return std::make_unique<FixedTemperature>(spec.value);
  if (spec.type == "heat_flux") return std::make_unique<HeatFlux>(spec.value, conductivity);
  if (spec.type == "adiabatic") return std::make_unique<Adiabatic>();
  throw CaseConfigurationError("CaseBuilder: unsupported temperature boundary type \"" + spec.type +
                               "\"");
}

// P2-TURB-004: k/epsilon boundary conditions are *derived* from each
// patch's already-configured velocity boundary *type* -- there is no
// separate per-patch turbulence entry in boundaries.json (unlike
// temperature, which needed one because a thermal condition is not
// inferable from the velocity type). This keeps the case schema minimal
// and matches sections 18-19's own minimum-scope wall/inlet/outlet
// treatment: wall/moving_wall patches get the documented simplified wall
// treatment (k=0, epsilon zero-gradient -- section 17 explicitly defers
// real wall functions); inlet patches get the case's configured
// initial_k/initial_epsilon as a fixed inflow value (standing in for a
// real turbulence-intensity-derived inlet condition, out of scope here);
// outlet/symmetry patches get zero-gradient for both, the same
// "unconstrained/mirrored" treatment Outlet/Symmetry's own velocity and
// FixedGradient(0.0) pressure conditions already use elsewhere in this
// file. Throws CaseConfigurationError for any velocity type this
// function does not recognize (mirrors buildVelocityBoundary's own
// default-throws convention) -- buildVelocityBoundary above already
// validates the type is one of the five supported ones before this ever
// runs, so this default path is unreachable in practice, not a silently
// accepted new case.
std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildTurbulenceKBoundary(
    const VelocityBoundarySpec& velocitySpec, Real inletK) {
  if (velocitySpec.type == "wall" || velocitySpec.type == "moving_wall") {
    return std::make_unique<FixedValue>(0.0);
  }
  if (velocitySpec.type == "inlet") {
    return std::make_unique<FixedValue>(inletK);
  }
  if (velocitySpec.type == "outlet" || velocitySpec.type == "symmetry") {
    return std::make_unique<FixedGradient>(0.0);
  }
  throw CaseConfigurationError("CaseBuilder: unsupported velocity boundary type \"" +
                               velocitySpec.type + "\" for turbulence k boundary derivation");
}

std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildTurbulenceEpsilonBoundary(
    const VelocityBoundarySpec& velocitySpec, Real inletEpsilon) {
  if (velocitySpec.type == "wall" || velocitySpec.type == "moving_wall") {
    // No validated wall-function value without near-wall treatment
    // (section 17/19) -- zero-gradient, documented as a simplification,
    // not a claimed high-Re wall-function epsilon.
    return std::make_unique<FixedGradient>(0.0);
  }
  if (velocitySpec.type == "inlet") {
    return std::make_unique<FixedValue>(inletEpsilon);
  }
  if (velocitySpec.type == "outlet" || velocitySpec.type == "symmetry") {
    return std::make_unique<FixedGradient>(0.0);
  }
  throw CaseConfigurationError("CaseBuilder: unsupported velocity boundary type \"" +
                               velocitySpec.type + "\" for turbulence epsilon boundary derivation");
}

// P2-TURB-005: the k-omega counterpart of buildTurbulenceEpsilonBoundary
// above, same derivation and same "no validated wall-function value"
// disclosure (section 20 -- k-omega's usual near-wall advantage over
// high-Re k-epsilon is not claimed here either, since no wall-distance-
// based treatment is implemented).
std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildTurbulenceOmegaBoundary(
    const VelocityBoundarySpec& velocitySpec, Real inletOmega) {
  if (velocitySpec.type == "wall" || velocitySpec.type == "moving_wall") {
    return std::make_unique<FixedGradient>(0.0);
  }
  if (velocitySpec.type == "inlet") {
    return std::make_unique<FixedValue>(inletOmega);
  }
  if (velocitySpec.type == "outlet" || velocitySpec.type == "symmetry") {
    return std::make_unique<FixedGradient>(0.0);
  }
  throw CaseConfigurationError("CaseBuilder: unsupported velocity boundary type \"" +
                               velocitySpec.type + "\" for turbulence omega boundary derivation");
}

// P2-TURB-006: the SST counterpart of buildTurbulenceOmegaBoundary above.
// Deliberately NOT the same wall treatment: SST already computes a real
// wall-distance field (turbulence::computeWallDistance, built once inside
// SSTModel's constructor), so wall/moving_wall patches get the real
// Wilcox near-wall omega Dirichlet value (WallOmega, omega_wall =
// 60*nu/(beta1*y^2)) instead of KOmegaModel's documented zero-gradient
// simplification -- see WallOmega.hpp's own header comment and
// SimulationSetup.hpp's omegaBoundaries comment for why the two models
// cannot share one builder despite both populating "omegaBoundaries".
// inlet/outlet/symmetry treatment is otherwise identical to k-omega's.
std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildTurbulenceOmegaBoundarySST(
    const VelocityBoundarySpec& velocitySpec, Real inletOmega, Real kinematicViscosity,
    Real beta1) {
  if (velocitySpec.type == "wall" || velocitySpec.type == "moving_wall") {
    return std::make_unique<WallOmega>(kinematicViscosity, beta1);
  }
  if (velocitySpec.type == "inlet") {
    return std::make_unique<FixedValue>(inletOmega);
  }
  if (velocitySpec.type == "outlet" || velocitySpec.type == "symmetry") {
    return std::make_unique<FixedGradient>(0.0);
  }
  throw CaseConfigurationError("CaseBuilder: unsupported velocity boundary type \"" +
                               velocitySpec.type + "\" for turbulence omega boundary derivation");
}

// Reuses the case's single momentum-solver tolerance/iteration budget
// for both the k and epsilon inner linear solves, rather than exposing a
// third/fourth independent LinearSolverSettings pair in physics.json --
// deliberately keeping the case schema's numerical-control surface
// small for this task (relaxation is still independently configurable
// via "k_relaxation"/"epsilon_relaxation", since that is a turbulence-
// specific stability concern the momentum solve has no equivalent of).
KEpsilonConfig buildKEpsilonConfig(const TurbulencePhysicsConfig& turbulence,
                                   const SolverConfig& solver) {
  KEpsilonConfig config;
  config.initialK = turbulence.initialK;
  config.initialEpsilon = *turbulence.initialEpsilon;
  config.kRelaxation = turbulence.kRelaxation.value_or(config.kRelaxation);
  config.epsilonRelaxation = turbulence.epsilonRelaxation.value_or(config.epsilonRelaxation);
  config.kSolver.absoluteTolerance = solver.momentumSolver.absoluteTolerance;
  config.kSolver.relativeTolerance = solver.momentumSolver.relativeTolerance;
  config.kSolver.maxIterations = solver.momentumSolver.maxIterations;
  config.epsilonSolver = config.kSolver;
  return config;
}

// P2-TURB-005: the k-omega counterpart of buildKEpsilonConfig above --
// same reasoning for reusing SolverConfig::momentumSolver's tolerances
// rather than a separate pair.
KOmegaConfig buildKOmegaConfig(const TurbulencePhysicsConfig& turbulence,
                               const SolverConfig& solver) {
  KOmegaConfig config;
  config.initialK = turbulence.initialK;
  config.initialOmega = *turbulence.initialOmega;
  config.kRelaxation = turbulence.kRelaxation.value_or(config.kRelaxation);
  config.omegaRelaxation = turbulence.omegaRelaxation.value_or(config.omegaRelaxation);
  config.kSolver.absoluteTolerance = solver.momentumSolver.absoluteTolerance;
  config.kSolver.relativeTolerance = solver.momentumSolver.relativeTolerance;
  config.kSolver.maxIterations = solver.momentumSolver.maxIterations;
  config.omegaSolver = config.kSolver;
  return config;
}

// P2-TURB-006: the SST counterpart of buildKOmegaConfig above -- same
// reasoning for reusing SolverConfig::momentumSolver's tolerances rather
// than a separate pair, and same relaxation-factor handling as k-omega
// (SST shares its k/omega relaxation field names in physics.json).
// coefficients/kFloor/omegaFloor are left at SSTConfig's own documented
// defaults -- this task does not expose them as case-file fields (no
// established precedent elsewhere in physics.json for exposing per-model
// numerical constants, and the acceptance gate does not ask for it).
SSTConfig buildSSTConfig(const TurbulencePhysicsConfig& turbulence, const SolverConfig& solver) {
  SSTConfig config;
  config.initialK = turbulence.initialK;
  config.initialOmega = *turbulence.initialOmega;
  config.kRelaxation = turbulence.kRelaxation.value_or(config.kRelaxation);
  config.omegaRelaxation = turbulence.omegaRelaxation.value_or(config.omegaRelaxation);
  config.kSolver.absoluteTolerance = solver.momentumSolver.absoluteTolerance;
  config.kSolver.relativeTolerance = solver.momentumSolver.relativeTolerance;
  config.kSolver.maxIterations = solver.momentumSolver.maxIterations;
  config.omegaSolver = config.kSolver;
  return config;
}

SIMPLESettings buildSolverSettings(const SolverConfig& solver) {
  SIMPLESettings settings;
  settings.maxIterations = solver.maxIterations;
  settings.velocityRelaxation = solver.velocityRelaxation;
  settings.pressureRelaxation = solver.pressureRelaxation;
  settings.velocityTolerance = solver.velocityTolerance;
  settings.pressureTolerance = solver.pressureTolerance;
  settings.continuityTolerance = solver.continuityTolerance;
  settings.momentumSolver.absoluteTolerance = solver.momentumSolver.absoluteTolerance;
  settings.momentumSolver.relativeTolerance = solver.momentumSolver.relativeTolerance;
  settings.momentumSolver.maxIterations = solver.momentumSolver.maxIterations;
  settings.pressureSolver.absoluteTolerance = solver.pressureSolver.absoluteTolerance;
  settings.pressureSolver.relativeTolerance = solver.pressureSolver.relativeTolerance;
  settings.pressureSolver.maxIterations = solver.pressureSolver.maxIterations;
  return settings;
}

}  // namespace

SimulationSetup CaseBuilder::build(const CaseDefinition& definition) const {
  // Only geometry=rectangle + mesh=structured_cartesian exist in this
  // phase's supported set (CaseReader's parsers already reject anything
  // else), so this is the only construction path -- not a dispatch that
  // needs its own "unsupported combination" branch (TODO.md P1 section
  // 42's "geometry dimensions incompatible with mesh configuration" cross
  // check has nothing left to check once both are already this
  // constrained).
  Mesh mesh =
      MeshGeometry::createCartesian2D(definition.mesh.nx, definition.mesh.ny,
                                      definition.geometry.length, definition.geometry.height);
  FluidProperties fluid(definition.physics.density, definition.physics.dynamicViscosity);

  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  // Built in mesh patch order (TODO.md P1 section 39), looking each
  // patch up by name in the (deterministically-ordered, std::map-backed)
  // BoundaryConfig rather than iterating boundaries.json's own object --
  // this is also where CaseReader's cross-file guarantee ("every mesh
  // patch has a configured entry") gets exercised: .at() below cannot
  // throw std::out_of_range because of that guarantee, not because of
  // anything checked again here.
  // P2-THERMAL-004: built alongside velocity/pressure in the same
  // per-patch loop rather than a second pass, so all three boundary sets
  // are built from one mesh-patch-order traversal. Only populated when
  // thermal is enabled -- CaseReader's own per-patch validation already
  // guarantees patchConfig.temperature has a value whenever
  // definition.physics.thermal does (P2-THERMAL-004's parseBoundaryConfig
  // requires "temperature" precisely iff thermalEnabled was passed in as
  // definition.physics.thermal.has_value()), so .value() below cannot
  // throw std::bad_optional_access because of that guarantee.
  const bool thermalEnabled = definition.physics.thermal.has_value();
  std::optional<ThermalProperties> thermal;
  std::optional<BoundaryConditionSet> temperatureBoundaries;
  if (thermalEnabled) {
    thermal.emplace(definition.physics.thermal->conductivity,
                    definition.physics.thermal->specificHeat);
    temperatureBoundaries.emplace();
  }

  // P6-PHYS-001: built alongside thermal above -- one SpeciesSetup per
  // physics.json-declared species, in declaration order. properties/
  // initialConcentration come straight from physics.json (no per-patch
  // dependency), so they are built here, before the per-patch loop;
  // concentrationBoundaries is filled in during that loop below, same
  // "built alongside velocity/pressure in one mesh-patch-order traversal"
  // reasoning as temperatureBoundaries. CaseReader's own per-patch
  // validation already guarantees patchConfig.concentration has an entry
  // for every declared species name (BoundaryConfigParser.cpp's own
  // "exactly the declared species-name set, on every patch" check), so
  // .at() in the loop below cannot throw std::out_of_range because of
  // that guarantee, not because of anything checked again here.
  std::vector<SpeciesSetup> speciesSetups;
  speciesSetups.reserve(definition.physics.species.size());
  for (const auto& speciesConfig : definition.physics.species) {
    speciesSetups.push_back(SpeciesSetup{
        .properties = SpeciesProperties(speciesConfig.name, speciesConfig.diffusivity),
        .initialConcentration =
            cfd::fields::ScalarField(mesh.numberOfCells(), speciesConfig.initialConcentration),
        .concentrationBoundaries = BoundaryConditionSet{}});
  }

  // P6-PHYS-002: built alongside species above -- system/initialAlpha
  // come straight from physics.json (no per-patch dependency);
  // alphaBoundaries is filled in during the per-patch loop below.
  const bool multiphaseEnabled = definition.physics.multiphase.has_value();
  std::optional<TwoPhaseSystem> multiphaseSystem;
  std::optional<cfd::fields::ScalarField> multiphaseInitialAlpha;
  std::optional<BoundaryConditionSet> alphaBoundaries;
  if (multiphaseEnabled) {
    const auto& m = *definition.physics.multiphase;
    multiphaseSystem.emplace(PhaseProperties(m.phase1.name, m.phase1.density, m.phase1.viscosity),
                             PhaseProperties(m.phase2.name, m.phase2.density, m.phase2.viscosity));
    multiphaseInitialAlpha.emplace(mesh.numberOfCells(), m.initialAlpha);
    alphaBoundaries.emplace();
  }

  // P2-TURB-004 (extended by P2-TURB-005 and P2-TURB-006):
  // "k_epsilon"/"k_omega"/"sst" are the only models that need anything
  // built here -- an absent "turbulence" block or an explicit "model":
  // "laminar" need no k/epsilon/omega boundary sets or KEpsilonConfig/
  // KOmegaConfig/SSTConfig at all (see SimulationSetup.hpp's own header
  // comment on why those collapse to the same all-nullopt state). The
  // three are mutually exclusive by construction (PhysicsConfigParser.cpp's
  // own "exactly one of initial_epsilon/initial_omega" validation plus
  // "model" being a single string), so at most one of
  // kEpsilonEnabled/kOmegaEnabled/sstEnabled is ever true.
  const bool kEpsilonEnabled = definition.physics.turbulence.has_value() &&
                               definition.physics.turbulence->model == "k_epsilon";
  const bool kOmegaEnabled = definition.physics.turbulence.has_value() &&
                             definition.physics.turbulence->model == "k_omega";
  const bool sstEnabled =
      definition.physics.turbulence.has_value() && definition.physics.turbulence->model == "sst";
  std::optional<BoundaryConditionSet> kBoundaries;
  std::optional<BoundaryConditionSet> epsilonBoundaries;
  std::optional<BoundaryConditionSet> omegaBoundaries;
  if (kEpsilonEnabled) {
    kBoundaries.emplace();
    epsilonBoundaries.emplace();
  } else if (kOmegaEnabled || sstEnabled) {
    kBoundaries.emplace();
    omegaBoundaries.emplace();
  }

  for (const auto& patch : mesh.boundaryPatches()) {
    const PatchBoundaryConfig& patchConfig = definition.boundaries.patches.at(patch.name());
    velocityBoundaries.set(mesh, patch.name(), buildVelocityBoundary(patchConfig.velocity));
    pressureBoundaries.set(mesh, patch.name(), buildPressureBoundary(patchConfig.pressure));
    if (thermalEnabled) {
      temperatureBoundaries->set(
          mesh, patch.name(),
          buildTemperatureBoundary(patchConfig.temperature.value(), thermal->conductivity()));
    }
    for (SpeciesSetup& speciesSetup : speciesSetups) {
      speciesSetup.concentrationBoundaries.set(
          mesh, patch.name(),
          buildConcentrationBoundary(patchConfig.concentration.at(speciesSetup.properties.name())));
    }
    if (multiphaseEnabled) {
      alphaBoundaries->set(mesh, patch.name(), buildAlphaBoundary(patchConfig.alpha.value()));
    }
    if (kEpsilonEnabled) {
      kBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceKBoundary(patchConfig.velocity, definition.physics.turbulence->initialK));
      epsilonBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceEpsilonBoundary(patchConfig.velocity,
                                         *definition.physics.turbulence->initialEpsilon));
    } else if (kOmegaEnabled) {
      kBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceKBoundary(patchConfig.velocity, definition.physics.turbulence->initialK));
      omegaBoundaries->set(mesh, patch.name(),
                           buildTurbulenceOmegaBoundary(
                               patchConfig.velocity, *definition.physics.turbulence->initialOmega));
    } else if (sstEnabled) {
      kBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceKBoundary(patchConfig.velocity, definition.physics.turbulence->initialK));
      omegaBoundaries->set(mesh, patch.name(),
                           buildTurbulenceOmegaBoundarySST(
                               patchConfig.velocity, *definition.physics.turbulence->initialOmega,
                               fluid.kinematicViscosity(), SSTConfig{}.coefficients.beta1));
    }
  }

  const cfd::Index numberOfCells = mesh.numberOfCells();
  cfd::fields::VectorField initialVelocity(numberOfCells, definition.initialConditions.velocity);
  cfd::fields::ScalarField initialPressure(numberOfCells, definition.initialConditions.pressure);

  // Every field named explicitly (including the three thermal ones, left
  // at their default-constructed empty std::optional here) -- aggregate
  // init with a partial positional list would otherwise warn
  // (-Wmissing-field-initializers, part of this project's -Wextra) about
  // the trailing fields it leaves default-initialized anyway.
  SimulationSetup setup{.mesh = std::move(mesh),
                        .fluid = fluid,
                        .velocityBoundaries = std::move(velocityBoundaries),
                        .pressureBoundaries = std::move(pressureBoundaries),
                        .solverSettings = buildSolverSettings(definition.solver),
                        .initialVelocity = std::move(initialVelocity),
                        .initialPressure = std::move(initialPressure),
                        .thermal = std::nullopt,
                        .temperatureBoundaries = std::nullopt,
                        .initialTemperature = std::nullopt,
                        .buoyancy = std::nullopt,
                        .kEpsilonConfig = std::nullopt,
                        .kOmegaConfig = std::nullopt,
                        .sstConfig = std::nullopt,
                        .kBoundaries = std::nullopt,
                        .epsilonBoundaries = std::nullopt,
                        .omegaBoundaries = std::nullopt,
                        .species = {},
                        .multiphase = std::nullopt,
                        .compressible = std::nullopt};
  if (thermalEnabled) {
    setup.thermal = thermal;
    setup.temperatureBoundaries = std::move(temperatureBoundaries);
    setup.initialTemperature =
        cfd::fields::ScalarField(numberOfCells, definition.physics.thermal->initialTemperature);
  }
  // P6-PHYS-001: unconditional (rather than an `if (!speciesSetups.empty())`
  // guard like thermal's own `if (thermalEnabled)` above) -- an empty
  // vector is already the correct "no species" representation (see
  // SimulationSetup.hpp's own header comment on SpeciesSetup), so there is
  // no separate empty/non-empty branch needed here.
  setup.species = std::move(speciesSetups);
  // P3-PHYS-001: "buoyancy" requires "thermal" (PhysicsConfigParser.cpp's
  // own cross-block check), so thermalEnabled is always true whenever
  // this is -- referenceDensity comes from the same single-sourced
  // FluidProperties::density() every other consumer here already uses,
  // never duplicated onto BuoyancyPhysicsConfig itself (see
  // PhysicsConfig.hpp's own header comment).
  if (definition.physics.buoyancy.has_value()) {
    setup.buoyancy.emplace(fluid.density(), definition.physics.buoyancy->beta,
                           definition.physics.buoyancy->referenceTemperature,
                           definition.physics.buoyancy->gravity);
  }
  if (kEpsilonEnabled) {
    setup.kEpsilonConfig = buildKEpsilonConfig(*definition.physics.turbulence, definition.solver);
    setup.kBoundaries = std::move(kBoundaries);
    setup.epsilonBoundaries = std::move(epsilonBoundaries);
  } else if (kOmegaEnabled) {
    setup.kOmegaConfig = buildKOmegaConfig(*definition.physics.turbulence, definition.solver);
    setup.kBoundaries = std::move(kBoundaries);
    setup.omegaBoundaries = std::move(omegaBoundaries);
  } else if (sstEnabled) {
    setup.sstConfig = buildSSTConfig(*definition.physics.turbulence, definition.solver);
    setup.kBoundaries = std::move(kBoundaries);
    setup.omegaBoundaries = std::move(omegaBoundaries);
  }
  if (multiphaseEnabled) {
    setup.multiphase =
        MultiphaseSetup{.system = std::move(*multiphaseSystem),
                        .initialAlpha = std::move(*multiphaseInitialAlpha),
                        .alphaBoundaries = std::move(*alphaBoundaries),
                        .transportTimeStep = definition.physics.multiphase->transportTimeStep};
  }
  // P6-PHYS-003: no per-patch dependency (the post-hoc low-Mach pass
  // reuses the existing velocity/pressure BCs unchanged -- see
  // CompressiblePhysicsConfig's own header comment), so built directly
  // from physics.json here, not in the per-patch loop above.
  if (definition.physics.compressible.has_value()) {
    const auto& c = *definition.physics.compressible;
    setup.compressible = CompressibleSetup{
        .thermodynamics = ThermodynamicProperties(c.gasConstant, c.specificHeatPressure),
        .referencePressure = c.referencePressure,
        .temperature = c.temperature,
        .thermalCoupled = c.thermalCoupled};
  }
  return setup;
}

}  // namespace cfd::io
