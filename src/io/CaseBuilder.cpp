#include "cfd/io/CaseBuilder.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/thermal/Enthalpy.hpp"

#include <cmath>
#include <memory>
#include <string>

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
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

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

// G8: the sensible-enthalpy reference temperature for a reacting mixture, 298.15 K -- the
// temperature thermochemical formation enthalpies are tabulated at, so h_f and the sensible
// integral share one reference and the energy accounting P18.6 froze stays consistent.
constexpr Real kReactingReferenceTemperature = 298.15;
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
    const VelocityBoundarySpec& velocitySpec, Real inletK, bool wallFunctions) {
  if (velocitySpec.type == "wall" || velocitySpec.type == "moving_wall") {
    // P16: with a wall function, k is ZERO-GRADIENT at the wall and epsilon is
    // the imposed quantity. Imposing k = 0 as well over-constrains the wall
    // cell -- the standard Launder-Spalding arrangement fixes epsilon and lets
    // k respond to the modified production.
    //
    // Without wall functions this stays the pre-P16 k = 0, so every existing
    // case is unchanged.
    if (wallFunctions) {
      return std::make_unique<FixedGradient>(0.0);
    }
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
    const VelocityBoundarySpec& velocitySpec, Real inletOmega, Real kinematicViscosity, Real beta1,
    bool wallFunctions) {
  if (velocitySpec.type == "wall" || velocitySpec.type == "moving_wall") {
    // P16: with a wall function, omega is IMPOSED in the wall CELL. Keeping
    // WallOmega on the wall FACE would impose a second, different value for
    // the same physics -- WallOmega is 60 nu/(beta1 y^2), Wilcox's viscous-
    // sublayer asymptotic, while the wall function's omega is the log-layer
    // equilibrium u_tau*/(sqrt(C_mu) kappa y_P). They disagree by orders of
    // magnitude at log-layer spacing, which is exactly the point.
    //
    // WallOmega remains the low-Re path, unchanged, for wallFunctions = false.
    if (wallFunctions) {
      return std::make_unique<FixedGradient>(0.0);
    }
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
// P16: absent means off, which is the pre-P16 behaviour.
bool turbulenceWallFunctionsEnabled(const std::optional<TurbulencePhysicsConfig>& turbulence) {
  return turbulence.has_value() && turbulence->wallFunctions.value_or(false);
}

KEpsilonConfig buildKEpsilonConfig(const TurbulencePhysicsConfig& turbulence,
                                   const SolverConfig& solver) {
  KEpsilonConfig config;
  config.initialK = turbulence.initialK;
  config.initialEpsilon = *turbulence.initialEpsilon;
  // P16. The wall function forms u_tau* = C_mu^(1/4) sqrt(k) from the SAME
  // C_mu the model uses for mu_t; KEpsilonModel refuses a mismatch, so they
  // are tied together here rather than left to agree by luck.
  config.wallFunctions = turbulence.wallFunctions.value_or(false);
  config.wallFunctionCoefficients.cMu = config.coefficients.cMu;
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
  // P16: tied to beta*, the constant SST uses where k-epsilon uses C_mu.
  // SSTModel refuses a mismatch.
  config.wallFunctions = turbulence.wallFunctions.value_or(false);
  config.wallFunctionCoefficients.cMu = config.coefficients.betaStar;
  // P16b. The parser has already rejected "automatic" on any other model
  // and any wall_treatment without wall_functions, so this is a straight
  // mapping with no silent fallback: an unrecognised string cannot reach
  // here.
  const std::string wallTreatment = turbulence.wallTreatment.value_or("high_re");
  if (wallTreatment == "automatic") {
    config.wallTreatmentMode = cfd::turbulence::WallTreatmentMode::Automatic;
  } else if (wallTreatment == "automatic_spalding") {
    config.wallTreatmentMode = cfd::turbulence::WallTreatmentMode::AutomaticSpalding;
  } else if (wallTreatment == "automatic_momentum_flux") {
    config.wallTreatmentMode = cfd::turbulence::WallTreatmentMode::AutomaticMomentumFlux;
  }
  config.initialOmega = *turbulence.initialOmega;
  config.kRelaxation = turbulence.kRelaxation.value_or(config.kRelaxation);
  config.omegaRelaxation = turbulence.omegaRelaxation.value_or(config.omegaRelaxation);
  config.kSolver.absoluteTolerance = solver.momentumSolver.absoluteTolerance;
  config.kSolver.relativeTolerance = solver.momentumSolver.relativeTolerance;
  config.kSolver.maxIterations = solver.momentumSolver.maxIterations;
  config.omegaSolver = config.kSolver;
  return config;
}

// P6-GPU-002: LinearSolverSpec.type/backend are already validated to be
// one of exactly these strings (SolverConfigParser.cpp) -- this is a
// pure lookup, never an "else" branch that needs its own error.
cfd::algebra::LinearSolverType parseLinearSolverType(const std::string& type) {
  if (type == "CG") return cfd::algebra::LinearSolverType::CG;
  if (type == "GMRES") return cfd::algebra::LinearSolverType::GMRES;
  return cfd::algebra::LinearSolverType::BiCGSTAB;
}

cfd::algebra::LinearSolverBackend parseLinearSolverBackend(const std::string& backend) {
  return (backend == "GPU") ? cfd::algebra::LinearSolverBackend::GPU
                            : cfd::algebra::LinearSolverBackend::CPU;
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
  settings.momentumSolver.type = parseLinearSolverType(solver.momentumSolver.type);
  settings.momentumSolver.backend = parseLinearSolverBackend(solver.momentumSolver.backend);
  settings.pressureSolver.absoluteTolerance = solver.pressureSolver.absoluteTolerance;
  settings.pressureSolver.relativeTolerance = solver.pressureSolver.relativeTolerance;
  settings.pressureSolver.maxIterations = solver.pressureSolver.maxIterations;
  settings.pressureSolver.type = parseLinearSolverType(solver.pressureSolver.type);
  settings.pressureSolver.backend = parseLinearSolverBackend(solver.pressureSolver.backend);
  // P12-NUM-001: already validated to be one of exactly upwind/central/
  // linear_upwind/quick by SolverConfigParser.cpp -- reuses
  // cfd::discretization's own parser rather than a second lookup table
  // here (same "pure lookup, never an else branch that needs its own
  // error" reasoning as parseLinearSolverType/Backend above).
  settings.convectionScheme = cfd::discretization::parseConvectionScheme(solver.convectionScheme);
  // P12-NUM-002: already validated to be one of exactly green_gauss/
  // least_squares by SolverConfigParser.cpp -- same reasoning as
  // convectionScheme above.
  settings.gradientScheme = cfd::discretization::parseGradientScheme(solver.gradientScheme);
  // P12-NUM-003: already validated to be >= 0 by SolverConfigParser.cpp.
  settings.nonOrthogonalCorrections = solver.nonOrthogonalCorrections;
  // P12-NUM-004: solver.json's "robustness" block (default: all off).
  settings.robustness = solver.robustness;
  // P12-MESH-006: already validated (automatic | linear | rhie_chow).
  settings.faceFlux = cfd::pressure_velocity::parseFaceFluxScheme(solver.faceFlux);
  return settings;
}

}  // namespace

namespace {

// P12-MESH-001: mesh.json "structured_quad" -> MeshGeometry::
// createStructuredQuad2D, its cell-level validity failures reported as
// case-configuration errors, plus a tiling check: the cells must cover the
// geometry.json rectangle exactly once (their areas sum to length * height
// to a relative 1e-9).
Mesh buildStructuredQuadMesh(const CaseDefinition& definition) {
  try {
    Mesh mesh = MeshGeometry::createStructuredQuad2D(definition.mesh.nx, definition.mesh.ny,
                                                     definition.mesh.vertices);
    Real area = 0.0;
    for (const auto& cell : mesh.cells()) area += cell.volume();
    const Real expected = definition.geometry.length * definition.geometry.height;
    if (!(std::abs(area - expected) <= 1e-9 * expected)) {
      throw CaseConfigurationError(
          "mesh.json: the structured_quad cells do not tile the geometry.json rectangle (total "
          "cell area " +
          std::to_string(area) + " vs length * height " + std::to_string(expected) + ")");
    }
    return mesh;
  } catch (const InvalidArgumentError& e) {
    throw CaseConfigurationError(std::string("mesh.json: invalid structured_quad mesh: ") +
                                 e.what());
  }
}

// P12-MESH-003: mesh.json "multiblock" -> MeshGeometry::createMultiBlock2D,
// block names resolved to indices; every geometric failure (non-convex or
// inverted cells, non-coincident interfaces, coincident boundary faces,
// overlapping blocks) becomes a case-configuration error.
Mesh buildMultiBlockMesh(const CaseDefinition& definition) {
  cfd::mesh::MultiBlockSpec spec;
  const auto blockIndex = [&](const std::string& name) -> Index {
    for (std::size_t b = 0; b < definition.mesh.blocks.size(); ++b) {
      if (definition.mesh.blocks[b].name == name) return b;
    }
    throw CaseConfigurationError("mesh.json: unknown block '" + name + "'");
  };
  const auto sideOf = [](const std::string& side) {
    if (side == "left") return cfd::mesh::BlockSide::Left;
    if (side == "right") return cfd::mesh::BlockSide::Right;
    if (side == "bottom") return cfd::mesh::BlockSide::Bottom;
    if (side == "top") return cfd::mesh::BlockSide::Top;
    throw CaseConfigurationError("mesh.json: unknown block side '" + side + "'");
  };
  const auto refOf = [&](const MeshSideRefConfig& ref) {
    return cfd::mesh::BlockSideRef{blockIndex(ref.block), sideOf(ref.side)};
  };
  for (const auto& block : definition.mesh.blocks) {
    spec.blocks.push_back(cfd::mesh::BlockSpec{block.name, block.nx, block.ny, block.vertices});
  }
  for (const auto& iface : definition.mesh.interfaces) {
    spec.interfaces.push_back(
        cfd::mesh::BlockInterfaceSpec{refOf(iface.first), refOf(iface.second), iface.reversed});
  }
  for (const auto& patch : definition.mesh.patches) {
    cfd::mesh::BoundaryPatchSpec p;
    p.name = patch.name;
    for (const auto& ref : patch.sides) p.sides.push_back(refOf(ref));
    spec.patches.push_back(std::move(p));
  }
  try {
    return MeshGeometry::createMultiBlock2D(spec);
  } catch (const InvalidArgumentError& e) {
    throw CaseConfigurationError(std::string("mesh.json: invalid multiblock mesh: ") + e.what());
  }
}

// P12-MESH-002: mesh.json "grading" -> MeshGeometry::createGraded2D (the
// rectilinear generalization of createCartesian2D; a uniform axis is the
// Cartesian spacing bit for bit). Invalid gradings were already rejected by
// CaseReader; a failure here is still reported as a case error.
Mesh buildGradedMesh(const CaseDefinition& definition) {
  try {
    return MeshGeometry::createGraded2D(definition.mesh.nx, definition.mesh.ny,
                                        definition.geometry.length, definition.geometry.height,
                                        definition.mesh.grading->x, definition.mesh.grading->y);
  } catch (const InvalidArgumentError& e) {
    throw CaseConfigurationError(std::string("mesh.json: invalid grading: ") + e.what());
  }
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
  // P12-MESH-001: "structured_quad" builds the vertex-defined structured
  // quad mesh (CaseReader already checked the vertices against the
  // geometry.json rectangle); "structured_cartesian" is exactly the
  // pre-existing Cartesian construction.
  // P12-MESH-002: "grading" (structured_cartesian only) builds the graded
  // rectilinear mesh; without it the mesh is exactly the pre-existing
  // uniform Cartesian construction.
  // P12-MESH-006: a geometry.json "box" (with mesh.json nz, checked by
  // CaseReader) builds the 3D Cartesian hexahedral mesh
  // (MeshGeometry::createCartesian3D: patches xmin, xmax, ymin, ymax, zmin,
  // zmax).
  Mesh mesh = [&] {
    if (definition.geometry.type == "box") {
      return MeshGeometry::createCartesian3D(definition.mesh.nx, definition.mesh.ny,
                                             definition.mesh.nz, definition.geometry.length,
                                             definition.geometry.height, definition.geometry.depth);
    }
    if (definition.mesh.type == "structured_quad") return buildStructuredQuadMesh(definition);
    if (definition.mesh.type == "multiblock") return buildMultiBlockMesh(definition);
    if (definition.mesh.grading.has_value()) return buildGradedMesh(definition);
    return MeshGeometry::createCartesian2D(definition.mesh.nx, definition.mesh.ny,
                                           definition.geometry.length, definition.geometry.height);
  }();
  // Production mesh-validity gate (every mesh type): positive volumes and
  // areas, face ownership/connectivity, closure, orientation (usable
  // diffusion coefficients), patch coverage -- MeshQuality::evaluate. A
  // mesh that fails never reaches a solver.
  // P12-MESH-004: the report (metrics, warnings, fatal conditions with their
  // location) travels with the setup; an Invalid mesh is rejected here,
  // before any solver, with every fatal condition listed.
  cfd::mesh::MeshQualityReport meshQuality = cfd::mesh::MeshQuality::evaluate(mesh);
  if (!meshQuality.valid) {
    std::string detail;
    for (const auto& issue : meshQuality.issues) {
      if (issue.severity == cfd::mesh::MeshQualitySeverity::Fatal) {
        detail += "\n  " + cfd::mesh::formatMeshQualityIssue(issue);
      }
    }
    // P13-002: this rejection drives UI state. apps/gui's validateDraft used
    // to detect it with message.contains("mesh is invalid") -- a panel's
    // status decided by searching prose for a phrase, with no test of the
    // coupling. The diagnostic gives the same signal as data: field "quality"
    // on mesh.json is unambiguous and survives any rewording of the text.
    // The message itself is unchanged.
    throw CaseConfigurationError("mesh.json: the " + definition.mesh.type + " mesh is invalid (" +
                                     meshQuality.summaryLine() + "):" + detail,
                                 cfd::CaseDiagnostic{"mesh.json", "quality", "produce a valid mesh",
                                                     meshQuality.summaryLine()});
  }
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
  // P16: whether this case asked for k-epsilon wall functions. Derived once,
  // before the per-patch loop, because both the k wall condition and the model
  // config have to agree about it.
  const bool kEpsilonWallFunctions = turbulenceWallFunctionsEnabled(definition.physics.turbulence);
  const bool sstWallFunctions = kEpsilonWallFunctions;
  const bool thermalEnabled = definition.physics.thermal.has_value();
  std::optional<ThermalProperties> thermal;
  std::optional<BoundaryConditionSet> temperatureBoundaries;
  std::shared_ptr<const cfd::physics::TemperatureProperty> conductivityModel;
  std::shared_ptr<const cfd::physics::TemperatureProperty> specificHeatModel;
  if (thermalEnabled) {
    thermal.emplace(definition.physics.thermal->conductivity,
                    definition.physics.thermal->specificHeat);
    temperatureBoundaries.emplace();
    // P15: a property given as an object is a MODEL, and dropping it here
    // would leave the case claiming a capability the solve does not deliver
    // -- ProjectRunner selects the variable-property solve on exactly these
    // two being present. A property given as a number produces no model, and
    // that path is unchanged: `thermal` above already holds its value.
    //
    // TemperatureDependentProperty re-validates the coefficients and the
    // range at construction and throws InvalidArgumentError naming the
    // property; converted here to the case-facing error type, which is the
    // second of the two validation layers this file's header describes (the
    // parser checked structure, this checks the physics).
    const auto buildModel =
        [&](const std::optional<cfd::io::TemperaturePolynomialConfig>& config,
            const char* propertyName) -> std::shared_ptr<const cfd::physics::TemperatureProperty> {
      if (!config.has_value()) return nullptr;
      try {
        return std::make_shared<const cfd::thermal::TemperatureDependentProperty>(
            config->coefficients, config->minimumTemperature, config->maximumTemperature,
            propertyName);
      } catch (const InvalidArgumentError& error) {
        throw CaseConfigurationError(std::string("physics.json thermal.") + propertyName + ": " +
                                     error.what());
      }
    };
    conductivityModel = buildModel(definition.physics.thermal->conductivityModel, "conductivity");
    specificHeatModel = buildModel(definition.physics.thermal->specificHeatModel, "specific_heat");
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

  // ---------------------------------------------------------------------------------------
  // G8: the REACTING mixture, built from physics.json's "reacting" block.
  //
  // Every object here already exists and is qualified -- P18.1's MassFractionState, P18.3's
  // MixtureThermodynamics, P18.4's ReactionMechanism, P18.5's ReactionKinetics, P18.6's
  // FormationEnthalpies. What did not exist was any path from a case file to them. This is it.
  //
  // THE ORDER IS FORCED BY THE DEPENDENCIES and is not a style choice: the registry is the
  // authority for species identity, the mixture needs the registry, the formation enthalpies need
  // the mixture's molecular weights, the mechanism canonicalises against the registry, and the
  // kinetics bind to the mechanism BY NAME. Each constructor validates its own inputs, so a bad
  // case file is refused by the layer that owns the invariant rather than by a check here.
  // ---------------------------------------------------------------------------------------
  std::optional<ReactingSetup> reactingSetup;
  if (definition.physics.reacting.has_value()) {
    const cfd::io::ReactingPhysicsConfig& config = *definition.physics.reacting;

    std::vector<std::string> names;
    names.reserve(config.species.size());
    for (const cfd::io::ReactingSpeciesConfig& species : config.species) {
      names.push_back(species.name);
    }
    cfd::species::SpeciesRegistry registry(names);

    std::vector<cfd::species::SpeciesThermodynamics> thermodynamics;
    thermodynamics.reserve(config.species.size());
    std::vector<Real> molarFormation;
    molarFormation.reserve(config.species.size());
    std::vector<cfd::species::SpeciesProperties> transport;
    transport.reserve(config.species.size());
    std::vector<Real> uniformComposition;
    uniformComposition.reserve(config.species.size());

    for (const cfd::io::ReactingSpeciesConfig& species : config.species) {
      thermodynamics.push_back(cfd::species::SpeciesThermodynamics(
          species.molecularWeight,
          cfd::thermal::SensibleEnthalpy(cfd::thermal::TemperatureDependentProperty::constant(
                                             species.specificHeat, "cp_" + species.name),
                                         kReactingReferenceTemperature, 0.0)));
      // physics.json carries h_f per MASS [J/kg]; FormationEnthalpies::fromMolar wants MOLAR
      // [J/mol], which is the form thermochemical tables publish. hbar = M * h.
      molarFormation.push_back(species.formationEnthalpy * species.molecularWeight);
      transport.push_back(cfd::species::SpeciesProperties(species.name, species.diffusivity));
      uniformComposition.push_back(species.initialMassFraction);
    }

    cfd::species::MixtureThermodynamics mixture(registry, std::move(thermodynamics));
    cfd::species::FormationEnthalpies formation =
        cfd::species::FormationEnthalpies::fromMolar(mixture, molarFormation);

    std::vector<cfd::species::Reaction> reactions;
    reactions.reserve(config.reactions.size());
    std::vector<cfd::species::ReactionRateParameters> rateParameters;
    rateParameters.reserve(config.reactions.size());

    for (const cfd::io::ReactingReactionConfig& reaction : config.reactions) {
      std::vector<cfd::species::ReactionTerm> reactants;
      reactants.reserve(reaction.reactants.size());
      for (const cfd::io::ReactionTermConfig& term : reaction.reactants) {
        reactants.push_back(cfd::species::ReactionTerm{term.species, term.coefficient});
      }
      std::vector<cfd::species::ReactionTerm> products;
      products.reserve(reaction.products.size());
      for (const cfd::io::ReactionTermConfig& term : reaction.products) {
        products.push_back(cfd::species::ReactionTerm{term.species, term.coefficient});
      }
      // The ORDERS travel as ReactionTerm too -- same shape, different meaning. Empty means mass
      // action, which is what Reaction's constructor defaults to.
      std::vector<cfd::species::ReactionTerm> orders;
      orders.reserve(reaction.orders.size());
      for (const cfd::io::ReactionOrderConfig& order : reaction.orders) {
        orders.push_back(cfd::species::ReactionTerm{order.species, order.order});
      }

      reactions.push_back(cfd::species::Reaction(
          reaction.name, registry, reactants, products,
          reaction.reversible ? cfd::species::ReactionDirection::Reversible
                              : cfd::species::ReactionDirection::Irreversible,
          orders));

      // Brace-initialised in one expression: ReactionRateParameters has no default constructor
      // because ArrheniusParameters has none -- A, n and Ea are not defaultable.
      //
      // A reversible reaction REQUIRES reverse parameters and an irreversible one FORBIDS them
      // (ReactionKinetics' own rule). The case schema carries one Arrhenius set per reaction, so a
      // reversible reaction declared here is refused by ReactionKinetics -- deliberately, rather
      // than silently reusing the forward parameters for the reverse direction.
      rateParameters.push_back(cfd::species::ReactionRateParameters{
          reaction.name,
          cfd::species::ArrheniusParameters(reaction.preExponential, reaction.temperatureExponent,
                                            reaction.activationEnergy),
          std::nullopt});
    }

    cfd::species::ReactionMechanism mechanism(registry, std::move(reactions), mixture);
    cfd::species::ReactionKinetics kinetics(mechanism, std::move(rateParameters));

    // MassFractionState VALIDATES: bounds and sum-to-one, refusing rather than normalising. The
    // parser already checked the declared set sums to 1, so this is the second of the two
    // independent layers -- and it is the one that owns the invariant.
    cfd::species::MassFractionState initialComposition(registry, mesh.numberOfCells(),
                                                       uniformComposition);

    reactingSetup = ReactingSetup{
        .registry = registry,
        .mixture = std::move(mixture),
        .formation = std::move(formation),
        .mechanism = std::move(mechanism),
        .kinetics = std::move(kinetics),
        .initialComposition = std::move(initialComposition),
        .transport = std::move(transport),
        .initialTemperature =
            cfd::fields::ScalarField(mesh.numberOfCells(), config.initialTemperature),
        .timeStep = config.timeStep,
        .steps = config.steps,
        // TD-001: one set per species, filled in the per-patch loop below, same two-step
        // construction the passive `speciesSetups` already uses (declared empty here because the
        // patch loop needs the mesh's patch names).
        .compositionBoundaries =
            std::vector<cfd::boundary::BoundaryConditionSet>(config.species.size()),
    };
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
    // TD-001: the reacting path's per-species sets, built from the SAME per-patch "species" map and
    // the SAME buildConcentrationBoundary as the passive path above -- a mass fraction and a tracer
    // concentration are both a scalar with a fixed value or a fixed gradient, so reusing the
    // builder is correct rather than merely convenient. Indexed by species order, matching
    // ReactingSetup::compositionBoundaries' own contract.
    if (reactingSetup.has_value()) {
      const auto& reactingConfig = *definition.physics.reacting;
      for (std::size_t i = 0; i < reactingConfig.species.size(); ++i) {
        reactingSetup->compositionBoundaries[i].set(
            mesh, patch.name(),
            buildConcentrationBoundary(
                patchConfig.concentration.at(reactingConfig.species[i].name)));
      }
    }
    if (multiphaseEnabled) {
      alphaBoundaries->set(mesh, patch.name(), buildAlphaBoundary(patchConfig.alpha.value()));
    }
    if (kEpsilonEnabled) {
      kBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceKBoundary(patchConfig.velocity, definition.physics.turbulence->initialK,
                                   kEpsilonWallFunctions));
      epsilonBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceEpsilonBoundary(patchConfig.velocity,
                                         *definition.physics.turbulence->initialEpsilon));
    } else if (kOmegaEnabled) {
      // k-omega and SST keep the pre-P16 wall k: their near-wall treatment is
      // their own and is deliberately untouched by P16's k-epsilon step.
      kBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceKBoundary(patchConfig.velocity, definition.physics.turbulence->initialK,
                                   /*wallFunctions=*/false));
      omegaBoundaries->set(mesh, patch.name(),
                           buildTurbulenceOmegaBoundary(
                               patchConfig.velocity, *definition.physics.turbulence->initialOmega));
    } else if (sstEnabled) {
      kBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceKBoundary(patchConfig.velocity, definition.physics.turbulence->initialK,
                                   sstWallFunctions));
      omegaBoundaries->set(
          mesh, patch.name(),
          buildTurbulenceOmegaBoundarySST(
              patchConfig.velocity, *definition.physics.turbulence->initialOmega,
              fluid.kinematicViscosity(), SSTConfig{}.coefficients.beta1, sstWallFunctions));
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
                        .conductivityModel = nullptr,
                        .specificHeatModel = nullptr,
                        .buoyancy = std::nullopt,
                        .kEpsilonConfig = std::nullopt,
                        .kOmegaConfig = std::nullopt,
                        .sstConfig = std::nullopt,
                        .kBoundaries = std::nullopt,
                        .epsilonBoundaries = std::nullopt,
                        .omegaBoundaries = std::nullopt,
                        .species = {},
                        .multiphase = std::nullopt,
                        .compressible = std::nullopt,
                        .meshQuality = std::move(meshQuality)};
  if (thermalEnabled) {
    setup.thermal = thermal;
    setup.conductivityModel = std::move(conductivityModel);
    setup.specificHeatModel = std::move(specificHeatModel);
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

  // G8: the reacting mixture, handed over at the same point and for the same reason as species
  // above -- `setup` is constructed below the builders, so each block builds a local first.
  setup.reacting = std::move(reactingSetup);
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
  // P12-NUM-003: every turbulence model's k/epsilon/omega diffusion uses the
  // same non-orthogonal-correction switch as momentum (solver.json's
  // non_orthogonal_corrections / gradient_scheme).
  const auto turbulenceNonOrthogonal =
      cfd::pressure_velocity::nonOrthogonalOptions(setup.solverSettings);
  if (kEpsilonEnabled) {
    setup.kEpsilonConfig = buildKEpsilonConfig(*definition.physics.turbulence, definition.solver);
    setup.kEpsilonConfig->nonOrthogonal = turbulenceNonOrthogonal;
    setup.kBoundaries = std::move(kBoundaries);
    setup.epsilonBoundaries = std::move(epsilonBoundaries);
  } else if (kOmegaEnabled) {
    setup.kOmegaConfig = buildKOmegaConfig(*definition.physics.turbulence, definition.solver);
    setup.kOmegaConfig->nonOrthogonal = turbulenceNonOrthogonal;
    setup.kBoundaries = std::move(kBoundaries);
    setup.omegaBoundaries = std::move(omegaBoundaries);
  } else if (sstEnabled) {
    setup.sstConfig = buildSSTConfig(*definition.physics.turbulence, definition.solver);
    setup.sstConfig->nonOrthogonal = turbulenceNonOrthogonal;
    setup.kBoundaries = std::move(kBoundaries);
    setup.omegaBoundaries = std::move(omegaBoundaries);
  }

  // P16: resolve Pr_t exactly once, and only when a model exists to supply
  // mu_t. The parser has already rejected a non-positive value and refused the
  // key outright on a laminar case, so this is a straight resolution with no
  // silent fallback: an explicitly-given bad value never reaches here.
  //
  // A laminar case leaves this empty, which is what prevents the thermal path
  // manufacturing a turbulent conductivity for a flow that has no turbulence.
  if (kEpsilonEnabled || kOmegaEnabled || sstEnabled) {
    setup.turbulentPrandtl = definition.physics.turbulence->turbulentPrandtl.value_or(
        cfd::turbulence::kDefaultTurbulentPrandtl);
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
        .thermalCoupled = c.thermalCoupled,
        .coupled = c.coupled};
  }
  return setup;
}

}  // namespace cfd::io
