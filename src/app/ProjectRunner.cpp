#include "cfd/app/ProjectRunner.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Logger.hpp"
#include "cfd/core/Version.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CheckpointReader.hpp"
#include "cfd/io/CheckpointWriter.hpp"
#include "cfd/io/case/CaseFingerprint.hpp"
#include "cfd/multiphase/MultiphaseProperties.hpp"
#include "cfd/multiphase/VolumeFractionEquation.hpp"
#include "cfd/multiphase/VolumeFractionSolver.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLECheckpoint.hpp"
// TD-001: the reacting dispatch reuses P18.2's conservative transport assembly and P18.4-P18.6's
// kinetics, sources and heat release directly -- no parallel reacting stack.
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/ChemistryIntegrator.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/SpeciesTransport.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/transport/ScalarTransport.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"
#include "cfd/turbulence/WallTreatment.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"  // simpleStatusName -- P13-005 (L8)

namespace cfd::app {

using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;

ProjectRunStatus statusWithReactingOutcome(ProjectRunStatus flowStatus,
                                           bool reactingSolveFailed) noexcept {
  if (!reactingSolveFailed) {
    return flowStatus;
  }
  // Cancelled survives: see the header for why it is the more informative verdict.
  if (flowStatus == ProjectRunStatus::Cancelled) {
    return ProjectRunStatus::Cancelled;
  }
  return ProjectRunStatus::NumericalFailure;
}

int exitCodeFor(ProjectRunStatus status) noexcept {
  switch (status) {
    case ProjectRunStatus::Converged:
      return 0;
    case ProjectRunStatus::ApplicationError:
      return 1;
    case ProjectRunStatus::InvalidCase:
      return 2;
    case ProjectRunStatus::DidNotConverge:
      return 3;
    case ProjectRunStatus::NumericalFailure:
      return 4;
    case ProjectRunStatus::Cancelled:
      return 5;
  }
  return 1;
}

namespace {

bool anyNonFinite(const SIMPLEResult& result) {
  for (cfd::Index i = 0; i < result.velocity.size(); ++i) {
    if (!cfd::isFinite(result.velocity[i])) return true;
  }
  for (cfd::Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) return true;
  }
  return false;
}

std::string thermalStatusName(cfd::thermal::ThermalStatus status) {
  using cfd::thermal::ThermalStatus;
  switch (status) {
    case ThermalStatus::Converged:
      return "Converged";
    case ThermalStatus::MaxIterations:
      return "MaxIterations";
    case ThermalStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case ThermalStatus::NonFiniteState:
      return "NonFiniteState";
    case ThermalStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

// P6-PHYS-001: the species counterpart of thermalStatusName above.
std::string speciesStatusName(cfd::species::SpeciesStatus status) {
  using cfd::species::SpeciesStatus;
  switch (status) {
    case SpeciesStatus::Converged:
      return "Converged";
    case SpeciesStatus::MaxIterations:
      return "MaxIterations";
    case SpeciesStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case SpeciesStatus::NonFiniteState:
      return "NonFiniteState";
    case SpeciesStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

// P6-PHYS-002: the multiphase counterpart of thermalStatusName above.
std::string volumeFractionStatusName(cfd::multiphase::VolumeFractionStatus status) {
  using cfd::multiphase::VolumeFractionStatus;
  switch (status) {
    case VolumeFractionStatus::Converged:
      return "Converged";
    case VolumeFractionStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case VolumeFractionStatus::NonFiniteState:
      return "NonFiniteState";
    case VolumeFractionStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

// P6-PHYS-002: reuses SIMPLE's existing turbulenceModel effective-
// viscosity injection point (mu_eff = molecular + mu_t, see
// cfd::turbulence::TurbulenceModel.hpp's own header comment: this
// formula is generic, not turbulence-specific) to feed mu_mix(alpha)
// into momentum assembly -- see ProjectRunner.hpp's own header comment
// for the full justification and scope (mu_mix only, evaluated at the
// case's configured *initial* alpha and held fixed for the whole SIMPLE
// solve; rho_mix stays out of continuity entirely). name() reports this
// plainly as what it is, not as a turbulence model, so a case's own
// metadata.json/CLI report is never confusing about what actually ran.
// correct() is a deliberate no-op: mu_t here is a fixed, precomputed
// field (not something SIMPLE's own outer iterations should update),
// unlike a genuine RANS model's own state.
class MixtureViscosityModel final : public cfd::turbulence::TurbulenceModel {
 public:
  // The caller (ProjectRunner::run(), where the real mesh is available)
  // is responsible for calling
  // cfd::turbulence::validateTurbulentViscosityField(mesh,
  // turbulentViscosityField) before constructing this -- not repeated
  // here, since that check needs the mesh this constructor does not
  // itself take (mirrors KEpsilonModel/KOmegaModel/SSTModel's own
  // convention of trusting already-mesh-sized fields their own
  // constructors receive).
  explicit MixtureViscosityModel(cfd::fields::ScalarField turbulentViscosityField)
      : turbulentViscosity_(std::move(turbulentViscosityField)) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "multiphaseMixtureViscosity";
  }
  [[nodiscard]] const cfd::fields::ScalarField& turbulentViscosity() const override {
    return turbulentViscosity_;
  }
  void correct(const cfd::mesh::Mesh&, const cfd::fields::VectorField&,
               const cfd::fields::ScalarField&) override {}

 private:
  cfd::fields::ScalarField turbulentViscosity_;
};

ProjectRunStatus statusFor(SIMPLEStatus status) {
  switch (status) {
    case SIMPLEStatus::Converged:
      return ProjectRunStatus::Converged;
    case SIMPLEStatus::MaxIterations:
      return ProjectRunStatus::DidNotConverge;
    // P12-NUM-004: a stagnated solve ran correctly but stopped making
    // progress -- "did not converge" (results are written); a diverging one
    // is a numerical failure, like a non-finite state.
    case SIMPLEStatus::Stagnated:
      return ProjectRunStatus::DidNotConverge;
    case SIMPLEStatus::Cancelled:
      return ProjectRunStatus::Cancelled;
    case SIMPLEStatus::MomentumFailure:
    case SIMPLEStatus::PressureCorrectionFailure:
    case SIMPLEStatus::NonFiniteState:
    case SIMPLEStatus::InvalidConfiguration:
    case SIMPLEStatus::Diverging:
      return ProjectRunStatus::NumericalFailure;
  }
  return ProjectRunStatus::NumericalFailure;
}

// P12-COMP-002: same mapping convention as statusFor(SIMPLEStatus) above,
// applied to the coupled compressible solve's own status -- used only
// when compressible.coupled is set, in which case this solve's outcome
// (not the incompressible warm-start's) becomes the run's authoritative
// status.
ProjectRunStatus statusFor(cfd::compressible::CompressibleSIMPLEStatus status) {
  using cfd::compressible::CompressibleSIMPLEStatus;
  switch (status) {
    case CompressibleSIMPLEStatus::Converged:
      return ProjectRunStatus::Converged;
    case CompressibleSIMPLEStatus::MaxIterations:
    case CompressibleSIMPLEStatus::Stagnated:
      return ProjectRunStatus::DidNotConverge;
    case CompressibleSIMPLEStatus::MomentumFailure:
    case CompressibleSIMPLEStatus::PressureCorrectionFailure:
    case CompressibleSIMPLEStatus::NonFiniteState:
    case CompressibleSIMPLEStatus::InvalidConfiguration:
    case CompressibleSIMPLEStatus::Diverging:
      return ProjectRunStatus::NumericalFailure;
  }
  return ProjectRunStatus::NumericalFailure;
}

std::string compressibleSimpleStatusName(cfd::compressible::CompressibleSIMPLEStatus status) {
  using cfd::compressible::CompressibleSIMPLEStatus;
  switch (status) {
    case CompressibleSIMPLEStatus::Converged:
      return "Converged";
    case CompressibleSIMPLEStatus::MaxIterations:
      return "MaxIterations";
    case CompressibleSIMPLEStatus::MomentumFailure:
      return "MomentumFailure";
    case CompressibleSIMPLEStatus::PressureCorrectionFailure:
      return "PressureCorrectionFailure";
    case CompressibleSIMPLEStatus::NonFiniteState:
      return "NonFiniteState";
    case CompressibleSIMPLEStatus::InvalidConfiguration:
      return "InvalidConfiguration";
    case CompressibleSIMPLEStatus::Stagnated:
      return "Stagnated";
    case CompressibleSIMPLEStatus::Diverging:
      return "Diverging";
  }
  return "Unknown";
}

// ---------------------------------------------------------------------------
// P13-001 -- Restart / checkpointing
// ---------------------------------------------------------------------------

// A case whose flow solve carries turbulence state across outer iterations
// cannot be checkpointed by this format: k, epsilon/omega and mu_t are not in
// it, and the three models can only be constructed from uniform scalar
// initial values, so there is no API that could restore a saved field. A
// checkpoint written for such a case would resume from the wrong turbulence
// state and quietly produce a different answer.
//
// Refused at BOTH ends -- writing and resuming -- and named, rather than
// written silently and discovered later. Recorded as technical debt in
// TODO.md; see results/p13/restart/audit.md section 2.5.
constexpr const char* kTurbulenceRefusal =
    "checkpointing is not supported for a case with a transport-equation turbulence model "
    "(k_epsilon, k_omega or sst): the model's own k/epsilon/omega state is not part of the "
    "checkpoint format, so a resumed run would continue from the wrong turbulence state. "
    "Run the case without checkpointing, or use a laminar case.";

// Runs the flow solve, segmenting it and writing checkpoints when the caller
// asked for that. With no checkpoint options set this is ONE solve() call
// with a null continuation -- structurally identical to the pre-P13 code
// path, not merely numerically equal to it.
//
// `checkpointError` is set (and the returned result is whatever the solve had
// reached) when checkpoint I/O or validation fails. A checkpoint problem is
// never swallowed: reporting success for a run whose checkpoint was not
// written is the one failure mode a checkpoint must not have.
SIMPLEResult runSimpleWithCheckpointing(const std::filesystem::path& caseDirectory,
                                        const cfd::io::SimulationSetup& setup,
                                        cfd::turbulence::TurbulenceModel* turbulenceModel,
                                        bool transportTurbulence, const ProjectRunOptions& options,
                                        ProjectRunResult& out, std::string& checkpointError) {
  using cfd::pressure_velocity::SIMPLECheckpoint;
  using cfd::pressure_velocity::SIMPLEContinuation;

  const bool wantsCheckpointing = options.resumeFrom.has_value() ||
                                  options.checkpointPath.has_value() ||
                                  options.checkpointInterval > 0;

  if (!wantsCheckpointing) {
    // Exactly the pre-P13 call: one solve, a null continuation, nothing else.
    const cfd::pressure_velocity::SIMPLE simple(
        setup.solverSettings, /*referenceCell=*/0, turbulenceModel, /*temperature=*/nullptr,
        /*buoyancy=*/nullptr, options.progressCallback, options.cancellationCheck);
    return simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                        setup.initialVelocity, setup.initialPressure);
  }

  if (transportTurbulence) {
    checkpointError = kTurbulenceRefusal;
    return SIMPLEResult{};
  }

  std::string caseFingerprint;
  try {
    caseFingerprint = cfd::io::computeCaseFingerprint(caseDirectory);
  } catch (const cfd::Error& e) {
    checkpointError = std::string("could not fingerprint the case for checkpointing: ") + e.what();
    return SIMPLEResult{};
  }

  const std::string backendName =
      setup.solverSettings.pressureSolver.backend == cfd::algebra::LinearSolverBackend::GPU ? "GPU"
                                                                                            : "CPU";

  // --- resume ---------------------------------------------------------------
  cfd::fields::VectorField velocity = setup.initialVelocity;
  cfd::fields::ScalarField pressure = setup.initialPressure;
  SIMPLEContinuation continuation;
  if (options.resumeFrom.has_value()) {
    try {
      const SIMPLECheckpoint checkpoint =
          cfd::io::CheckpointReader::read(*options.resumeFrom, setup.mesh);
      cfd::io::CheckpointReader::requireCaseFingerprint(checkpoint, caseFingerprint,
                                                        *options.resumeFrom);
      velocity = checkpoint.velocity;
      pressure = checkpoint.pressure;
      continuation = cfd::pressure_velocity::makeContinuationFromCheckpoint(checkpoint);
      out.resumedFromIteration = checkpoint.completedIterations;
      cfd::Logger::instance().info("resumed from iteration " +
                                   std::to_string(checkpoint.completedIterations) + ": " +
                                   options.resumeFrom->string());
    } catch (const cfd::Error& e) {
      checkpointError = std::string("could not resume from checkpoint ") +
                        options.resumeFrom->string() + ": " + e.what();
      return SIMPLEResult{};
    }
  }

  const auto writeCheckpoint = [&](const SIMPLEResult& segment) {
    if (!options.checkpointPath.has_value()) return true;
    if (segment.iterations == 0) return true;  // nothing completed: nothing to save.
    try {
      const SIMPLECheckpoint checkpoint = cfd::pressure_velocity::makeSIMPLECheckpoint(
          segment, setup.mesh, caseFingerprint, backendName,
          std::string(cfd::core::versionString()));
      cfd::io::CheckpointWriter::write(*options.checkpointPath, checkpoint);
      ++out.checkpointsWritten;
      out.checkpointPath = *options.checkpointPath;
      // P13-005 (L4). Info: a user who asked for checkpointing wants to know
      // one landed, and it is the only evidence a long run leaves as it goes.
      cfd::Logger::instance().info("checkpoint written at iteration " +
                                   std::to_string(segment.iterations) + ": " +
                                   options.checkpointPath->string());
      return true;
    } catch (const cfd::Error& e) {
      // A solve that has ALREADY failed can leave a state no checkpoint could
      // represent -- a non-finite velocity or pressure after a NonFiniteState,
      // a momentum failure or a divergence. There is nothing worth resuming
      // from, and reporting "could not write checkpoint" would MASK the
      // numerical failure that is the real answer. So the write is skipped,
      // `checkpointsWritten` stays where it was, and the solve's own status
      // speaks -- the same policy ResultExporter already applies when it
      // declines to write fields.csv for a non-finite solution.
      //
      // The same refusal on a segment that did NOT fail is a real error and is
      // reported: there the state was resumable and the checkpoint is missing.
      const bool solveAlreadyFailed = segment.status != SIMPLEStatus::Converged &&
                                      segment.status != SIMPLEStatus::MaxIterations &&
                                      segment.status != SIMPLEStatus::Cancelled;
      if (solveAlreadyFailed) return true;
      checkpointError = std::string("could not write checkpoint ") +
                        options.checkpointPath->string() + ": " + e.what();
      return false;
    }
  };

  // --- the segmented solve --------------------------------------------------
  //
  // Each pass is one SIMPLE::solve() call. A segment that ends with
  // MaxIterations while its iteration count is still below the configured
  // budget stopped because it exhausted THIS segment's budget, not the run's
  // -- that pair is unambiguous (SIMPLEContinuation.hpp) -- so the loop
  // checkpoints and continues. Any other status, or reaching the budget, ends
  // the run.
  const cfd::Index total = setup.solverSettings.maxIterations;
  SIMPLEResult result;
  while (true) {
    continuation.iterationBudget = options.checkpointInterval;
    cfd::pressure_velocity::SIMPLE segmentSolver(
        setup.solverSettings, /*referenceCell=*/0, turbulenceModel, /*temperature=*/nullptr,
        /*buoyancy=*/nullptr, options.progressCallback, options.cancellationCheck);
    segmentSolver.setContinuation(&continuation);
    result = segmentSolver.solve(setup.mesh, setup.fluid, setup.velocityBoundaries,
                                 setup.pressureBoundaries, velocity, pressure);

    const bool more = result.status == SIMPLEStatus::MaxIterations && result.iterations < total &&
                      options.checkpointInterval > 0;
    if (!writeCheckpoint(result)) return result;
    if (!more) break;

    velocity = result.velocity;
    pressure = result.pressure;
    try {
      continuation = cfd::pressure_velocity::makeSIMPLEContinuation(result);
    } catch (const cfd::Error& e) {
      checkpointError = std::string("could not continue after a checkpoint: ") + e.what();
      return result;
    }
  }
  return result;
}

}  // namespace

ProjectRunResult ProjectRunner::run(const std::filesystem::path& caseDirectory,
                                    const ProjectRunOptions& options) {
  ProjectRunResult out;

  std::optional<cfd::io::CaseDefinition> caseDefinition;
  std::optional<cfd::io::SimulationSetup> setupOpt;
  try {
    // P13-005 (L1, the loading half). Debug: which directory a run actually
    // read is the first question when a run solves the wrong problem.
    cfd::Logger::instance().debug("loading case: " + caseDirectory.string());
    caseDefinition = cfd::io::CaseReader{}.read(caseDirectory);
    setupOpt = cfd::io::CaseBuilder{}.build(*caseDefinition);
  } catch (const cfd::Error& e) {
    out.status = ProjectRunStatus::InvalidCase;
    out.errorMessage = e.what();
    // P13-005 (L1), amendment A1. Debug, not Error: the CLI prints this exact
    // text itself (measured -- logs/02_V4_FAIL_as_frozen.log), so an Error
    // line here would be a verbatim second copy of what the user is already
    // shown. ProjectRunner RETURNS every failure and the client reports it;
    // the log is the trail, never the outcome.
    cfd::Logger::instance().debug(std::string("case rejected: ") + e.what());
    // P13-002: the same rejection as data, when the thrower carried it. No
    // new catch and no change to which errors land here -- the existing
    // handler simply stops discarding what the exception already knew.
    out.caseDiagnostic = cfd::caseDiagnosticOf(e);
    return out;
  }
  out.caseDefinition = caseDefinition;
  const cfd::io::SimulationSetup& setup = *setupOpt;
  out.mesh = setup.mesh;
  out.meshQuality = setup.meshQuality;

  // Same turbulence-model construction/dispatch as the pre-P5 CLI
  // runCase() (see that function's own header comment for why this is
  // built here rather than by CaseBuilder).
  std::optional<cfd::turbulence::KEpsilonModel> kEpsilonModel;
  std::optional<cfd::turbulence::KOmegaModel> kOmegaModel;
  std::optional<cfd::turbulence::SSTModel> sstModel;
  cfd::turbulence::TurbulenceModel* activeTurbulenceModel = nullptr;
  if (setup.kEpsilonConfig.has_value()) {
    kEpsilonModel.emplace(setup.mesh, setup.fluid, setup.velocityBoundaries, *setup.kBoundaries,
                          *setup.epsilonBoundaries, *setup.kEpsilonConfig);
    activeTurbulenceModel = &(*kEpsilonModel);
  } else if (setup.kOmegaConfig.has_value()) {
    kOmegaModel.emplace(setup.mesh, setup.fluid, setup.velocityBoundaries, *setup.kBoundaries,
                        *setup.omegaBoundaries, *setup.kOmegaConfig);
    activeTurbulenceModel = &(*kOmegaModel);
  } else if (setup.sstConfig.has_value()) {
    sstModel.emplace(setup.mesh, setup.fluid, setup.velocityBoundaries, *setup.kBoundaries,
                     *setup.omegaBoundaries, *setup.sstConfig);
    activeTurbulenceModel = &(*sstModel);
  }

  // P6-PHYS-002: reuses the same turbulenceModel injection slot as
  // above -- mutually exclusive with real turbulence by construction
  // (PhysicsConfigParser.cpp's own cross-block rejection), so this
  // `else if` is never reached when any of the three models above
  // already claimed the slot. mu_mix is evaluated at the case's
  // *initial* alpha only (held fixed for this entire SIMPLE solve -- see
  // ProjectRunner.hpp's own header comment on why), never the alpha
  // this same run will go on to solve for below.
  std::optional<MixtureViscosityModel> mixtureViscosityModel;
  if (setup.multiphase.has_value() && activeTurbulenceModel == nullptr) {
    cfd::fields::ScalarField turbulentViscosityField =
        cfd::multiphase::evaluateMixtureViscosityField(setup.mesh, setup.multiphase->initialAlpha,
                                                       setup.multiphase->system);
    for (cfd::Index i = 0; i < turbulentViscosityField.size(); ++i) {
      turbulentViscosityField[i] -= setup.fluid.dynamicViscosity();
    }
    cfd::turbulence::validateTurbulentViscosityField(setup.mesh, turbulentViscosityField);
    mixtureViscosityModel.emplace(std::move(turbulentViscosityField));
    activeTurbulenceModel = &(*mixtureViscosityModel);
  }

  // P13-001 -- Restart / checkpointing. Off unless the caller asked for it,
  // in which case the flow solve below is executed as one or more segments
  // carrying a SIMPLEContinuation. See runSimpleWithCheckpointing.
  // P13-005 (L3 solver start, L2 the selection half): which case, which
  // solver, and which linear-solver backend was *asked for*. Debug, because a
  // default run's stderr must stay as quiet as it was -- criterion V4. The
  // fallback half of L2 is a Warning below, because a request that was not
  // honoured changes what ran and should not need --log-level to see.
  cfd::Logger::instance().debug(
      "solver start: " + caseDefinition->caseConfig.name + " (" + caseDefinition->solver.type +
      ", " + std::to_string(setup.mesh.numberOfCells()) + " cells, linear solver backend " +
      caseDefinition->solver.pressureSolver.backend + ")");
  SIMPLEResult result;
  {
    std::string checkpointError;
    const bool transportTurbulence = setup.kEpsilonConfig.has_value() ||
                                     setup.kOmegaConfig.has_value() || setup.sstConfig.has_value();
    result = runSimpleWithCheckpointing(caseDirectory, setup, activeTurbulenceModel,
                                        transportTurbulence, options, out, checkpointError);
    if (!checkpointError.empty()) {
      out.simpleResult = result;
      out.status = ProjectRunStatus::ApplicationError;
      out.errorMessage = checkpointError;
      return out;
    }
  }
  out.simpleResult = result;
  out.status = statusFor(result.status);
  // P13-005 (L8): the completion summary. Info, so a default run stays silent
  // and `--log-level info` gives a user the one line they actually want.
  // Everything here is already in the result -- this emits it rather than
  // computing anything, so it cannot disagree with the report or the export.
  {
    const auto& r = result;
    cfd::Logger::instance().info(
        "solve finished: " + std::string(cfd::validation::simpleStatusName(r.status)) + ", " +
        std::to_string(r.iterations) +
        " outer iterations, residuals u=" + std::to_string(r.finalUResidual) +
        " v=" + std::to_string(r.finalVResidual) + " p=" + std::to_string(r.finalPressureResidual) +
        " continuity=" + std::to_string(r.finalContinuityResidual) + ", mass imbalance " +
        std::to_string(r.globalMassImbalance) + ", " + std::to_string(r.stageSeconds.total) +
        " s, discretization " + (r.gpuDiscretization ? "GPU" : "CPU"));
    // L5/L6 (amendment A1): the failure itself is returned in the status and
    // printed by the client. These lines add what the status cannot say --
    // which solve failed, and why -- so they are Debug, not a second Error.
    if (r.status == SIMPLEStatus::MomentumFailure ||
        r.status == SIMPLEStatus::PressureCorrectionFailure) {
      cfd::Logger::instance().debug(
          "linear solve failed: " + std::string(cfd::validation::simpleStatusName(r.status)) +
          (r.robustness.statusDetail.empty() ? std::string() : " -- " + r.robustness.statusDetail));
    } else if (r.status == SIMPLEStatus::NonFiniteState) {
      cfd::Logger::instance().debug("solver state went non-finite at iteration " +
                                    std::to_string(r.iterations));
    }
    // L2's fallback half is NOT emitted here. SIMPLE.cpp already warns with
    // the reason at the point it declines (one of the two logging call sites
    // that existed before P13-005), and it fires for every caller, not only
    // this one. A second copy here would be exactly the duplication
    // amendment A1 removed elsewhere.
  }

  // massFlux is needed by both thermal and species below (P6-PHYS-001:
  // hoisted here, computed at most once, rather than each recomputing its
  // own copy) -- a pure function of setup.mesh/result.velocity/setup.fluid/
  // setup.velocityBoundaries, so sharing it changes nothing about either
  // solve's result.
  std::optional<cfd::fields::SurfaceField> massFlux;
  if ((setup.thermal.has_value() || !setup.species.empty() || setup.multiphase.has_value() ||
       setup.compressible.has_value()) &&
      !anyNonFinite(result)) {
    massFlux = cfd::physics::calculateMassFlux(setup.mesh, result.velocity, setup.fluid,
                                               setup.velocityBoundaries);
  }

  // Same one-way, best-available thermal solve as the pre-P5 CLI (see
  // apps/cli/main.cpp's own comment on this policy -- unchanged here).
  std::optional<cfd::thermal::ThermalResult> thermalResult;
  std::optional<cfd::io::ThermalRunMetadata> thermalMetadata;
  std::optional<TurbulentThermalRunResult> turbulentThermalResult;
  if (setup.thermal.has_value() && massFlux.has_value()) {
    // P12-NUM-003: same non-orthogonal-correction switch as momentum.
    cfd::thermal::ThermalSolverSettings thermalSettings;
    thermalSettings.nonOrthogonal =
        cfd::pressure_velocity::nonOrthogonalOptions(setup.solverSettings);
    const cfd::thermal::ThermalSolver thermalSolver{thermalSettings};
    // P15: a case whose physics.json gave a property MODEL takes the
    // variable-property overload (P3-PHYS-003), which re-evaluates k(T)/cp(T)
    // from the current iterate inside the SAME Picard loop -- not a second
    // nonlinear loop, and not a solve with the model quietly replaced by its
    // value at the initial temperature.
    //
    // A model may be given for one property and not the other, so the
    // constant side is wrapped in a ConstantProperty: the overload then
    // reproduces the fixed-property solve exactly for that property, which is
    // that class's documented purpose (P3-PHYS-003 section 4) rather than a
    // substitution invented here.
    //
    // Neither model present -> the original call, unchanged. That is the
    // overwhelmingly common path and it must stay bit-for-bit what it was.
    // P16 -- turbulent heat transfer.
    //
    // Enabled exactly when a REAL turbulence model supplied mu_t. The guard is
    // deliberately not `activeTurbulenceModel != nullptr`: that pointer can
    // also hold MixtureViscosityModel (P6-PHYS-002), which occupies the same
    // injection slot but is a multiphase mixture viscosity, not turbulence.
    // Closing a turbulent heat flux for it would invent turbulent conductivity
    // for a laminar flow (P16 section 12). setup.turbulentPrandtl is populated
    // by CaseBuilder only for k_epsilon/k_omega/sst, so it carries exactly the
    // right condition; the config checks below state it a second way rather
    // than relying on that one fact.
    const bool realTurbulenceModel = setup.kEpsilonConfig.has_value() ||
                                     setup.kOmegaConfig.has_value() || setup.sstConfig.has_value();
    const bool turbulentThermal = realTurbulenceModel && activeTurbulenceModel != nullptr &&
                                  setup.turbulentPrandtl.has_value();

    if (setup.conductivityModel != nullptr || setup.specificHeatModel != nullptr ||
        turbulentThermal) {
      const cfd::physics::ConstantProperty constantConductivity{setup.thermal->conductivity()};
      const cfd::physics::ConstantProperty constantSpecificHeat{setup.thermal->specificHeat()};
      const cfd::physics::TemperatureProperty& conductivity =
          setup.conductivityModel != nullptr
              ? static_cast<const cfd::physics::TemperatureProperty&>(*setup.conductivityModel)
              : static_cast<const cfd::physics::TemperatureProperty&>(constantConductivity);
      const cfd::physics::TemperatureProperty& specificHeat =
          setup.specificHeatModel != nullptr
              ? static_cast<const cfd::physics::TemperatureProperty&>(*setup.specificHeatModel)
              : static_cast<const cfd::physics::TemperatureProperty&>(constantSpecificHeat);
      if (turbulentThermal) {
        // mu_t is the CONVERGED field from the flow solve above and is not
        // re-corrected against the temperature produced below: the production
        // coupling is one-way, (u, p, mu_t) -> T. Temperature does not feed
        // back into momentum, density, viscosity or turbulence on this path.
        // See results/p16/turbulence/eddy_viscosity/known_limitations.md.
        cfd::thermal::TurbulentThermalCoupling coupling{
            &activeTurbulenceModel->turbulentViscosity(), *setup.turbulentPrandtl};

        // P16-TWF -- the thermal wall function, active only when the model
        // actually ran a wall treatment and therefore has a real wall shear to
        // offer.
        //
        // This block TRANSPORTS wall state; it computes no wall physics. In
        // particular u_tau is NOT reconstructed here from a velocity secant --
        // the model's own wall shear is handed over and
        // cfd::thermal::computeThermalWallFace derives u_tau = sqrt(tau_w/rho)
        // from it, so the thermal law is driven by the same wall the momentum
        // wall function produced.
        std::vector<cfd::Index> thermalWallFaceIds;
        if (activeTurbulenceModel->wallShearStressField() != nullptr &&
            activeTurbulenceModel->wallYPlusField() != nullptr) {
          for (const auto& wallFace :
               cfd::turbulence::collectWallFaces(setup.mesh, setup.velocityBoundaries)) {
            thermalWallFaceIds.push_back(wallFace.faceId);
          }
          coupling.wallShearStress = activeTurbulenceModel->wallShearStressField();
          coupling.yPlus = activeTurbulenceModel->wallYPlusField();
          coupling.wallFaceIds = &thermalWallFaceIds;
          coupling.density = setup.fluid.density();
          coupling.dynamicViscosity = setup.fluid.dynamicViscosity();
          // The SAME kappa/E the momentum wall function used, taken from the
          // active model's own config rather than re-defaulted here.
          if (setup.kEpsilonConfig.has_value()) {
            coupling.wallFunctionCoefficients = setup.kEpsilonConfig->wallFunctionCoefficients;
          } else if (setup.sstConfig.has_value()) {
            coupling.wallFunctionCoefficients = setup.sstConfig->wallFunctionCoefficients;
          }
        }
        thermalResult =
            thermalSolver.solve(setup.mesh, *setup.initialTemperature, *massFlux, conductivity,
                                specificHeat, *setup.temperatureBoundaries, coupling);
        // Recorded so the coupling's inputs are inspectable -- see
        // TurbulentThermalRunResult.
        turbulentThermalResult = TurbulentThermalRunResult{
            *setup.turbulentPrandtl, activeTurbulenceModel->turbulentViscosity()};
      } else {
        thermalResult =
            thermalSolver.solve(setup.mesh, *setup.initialTemperature, *massFlux, conductivity,
                                specificHeat, *setup.temperatureBoundaries);
      }
    } else {
      thermalResult = thermalSolver.solve(setup.mesh, *setup.initialTemperature, *massFlux,
                                          *setup.thermal, *setup.temperatureBoundaries);
    }
    thermalMetadata = cfd::io::ThermalRunMetadata{setup.thermal->conductivity(),
                                                  setup.thermal->specificHeat(),
                                                  thermalStatusName(thermalResult->status),
                                                  thermalResult->converged(),
                                                  thermalResult->iterations,
                                                  thermalResult->finalResidual};
  } else if (setup.thermal.has_value()) {
    thermalMetadata = cfd::io::ThermalRunMetadata{setup.thermal->conductivity(),
                                                  setup.thermal->specificHeat(),
                                                  "NotRun",
                                                  /*converged=*/false,
                                                  /*iterations=*/0,
                                                  /*finalResidual=*/0.0};
  }
  out.thermalResult = thermalResult;
  out.turbulentThermalResult = std::move(turbulentThermalResult);

  // P6-PHYS-001: same one-way, best-available policy as thermal above --
  // run only once the SIMPLE result is fully finite, one
  // cfd::species::SpeciesSolver::solve() call per declared species (never
  // a dedicated multi-species solver class, see SpeciesSolver.hpp's own
  // header comment), each independent of the others (this codebase's
  // species are passive/non-reacting -- no cross-species coupling term
  // exists to solve). out.speciesResults stays empty (not partially
  // populated) when massFlux was never computed; speciesMetadataForExport
  // still gets one "NotRun" entry per declared species either way, the
  // same "metadata always reflects the real outcome" policy thermal's
  // own metadata already follows.
  std::vector<cfd::io::SpeciesRunMetadata> speciesMetadataForExport;
  std::vector<cfd::io::NamedScalarField> speciesForExport;
  if (massFlux.has_value()) {
    // P12-NUM-003: same non-orthogonal-correction switch as momentum.
    cfd::species::SpeciesSolverSettings speciesSettings;
    speciesSettings.nonOrthogonal =
        cfd::pressure_velocity::nonOrthogonalOptions(setup.solverSettings);
    const cfd::species::SpeciesSolver speciesSolver{speciesSettings};
    for (const cfd::io::SpeciesSetup& speciesSetup : setup.species) {
      const cfd::species::SpeciesResult speciesResult =
          speciesSolver.solve(setup.mesh, speciesSetup.initialConcentration, *massFlux, setup.fluid,
                              speciesSetup.properties, speciesSetup.concentrationBoundaries);
      out.speciesResults.push_back(SpeciesRunResult{speciesSetup.properties.name(), speciesResult});
      speciesMetadataForExport.push_back(cfd::io::SpeciesRunMetadata{
          speciesSetup.properties.name(), speciesSetup.properties.diffusivity(),
          speciesStatusName(speciesResult.status), speciesResult.converged(),
          speciesResult.iterations, speciesResult.finalResidual});
      speciesForExport.emplace_back("concentration_" + speciesSetup.properties.name(),
                                    speciesResult.concentration);
    }
  } else {
    for (const cfd::io::SpeciesSetup& speciesSetup : setup.species) {
      speciesMetadataForExport.push_back(cfd::io::SpeciesRunMetadata{
          speciesSetup.properties.name(), speciesSetup.properties.diffusivity(), "NotRun",
          /*converged=*/false, /*iterations=*/0, /*finalResidual=*/0.0});
    }
  }

  // ---------------------------------------------------------------------------
  // TD-001 -- THE REACTING DISPATCH. Closes the CLAUDE.md section 9 gap.
  // ---------------------------------------------------------------------------
  //
  // Before this block `setup.reacting` had ZERO references in this file, so a case declaring a
  // mechanism produced a full result set with `species: []` and no reaction quantity at all
  // (reproducer.md variant 3). The configuration was parsed, validated and built into live
  // ReactionMechanism/ReactionKinetics objects by CaseBuilder, and then dropped.
  //
  // IT IS ONE OPERATOR-SPLIT TIME STEP of size `reacting.time_step`, and that is a MEASURED
  // conclusion, not a preference. A steady pass with the reaction source frozen at the initial
  // state was implemented first, and it has no source treatment that is both conservative and
  // bounded:
  //
  //     Patankar-split source   every Y_i >= 0, but max|sum Y - 1| = 3.00e-01
  //     fully explicit source   conserves to 4.92e-07, but Y_A reaches -0.297
  //
  // The asymmetry is structural rather than a coding error: a consumption term treated implicitly
  // self-limits as Y_i falls, while a production term stays at the rate evaluated from the initial
  // state, so `sum_i omega_i = 0` stops holding for the EFFECTIVE source even though P18.4
  // guarantees it for the source itself. Both failures are preserved in
  // results/technical_debt/TD001_reacting_production/verification.md, and gate amendment A1 uses
  // exactly these two variants as its controls A1-N1 and A1-N2.
  //
  // Splitting removes both, because neither half has to carry the other half's defect:
  //
  //   STEP 1  CHEMISTRY, local, over dt -- P18.6's already-qualified `advanceClosedReactor`, which
  //           applies `sum_i omega_i = 0` per cell and recovers T from the conserved energy. It
  //           REFUSES a step that would drive any rho*Y negative instead of clipping it, so
  //           positivity is a property of the step rather than a repair applied afterwards.
  //   STEP 2  TRANSPORT, with NO reaction source at all -- convection + diffusion + the implicit
  //           Euler transient term, an M-matrix, so every Y_i stays bounded; and because the
  //           species share one matrix here (equal diffusivities, equal boundary types) the summed
  //           equations have the constant 1 as their exact discrete solution, which is what gate
  //           criterion C4b tests by SCALING the linear tolerance.
  //
  // WHY CHEMISTRY FIRST, not transport first. Both orders are first-order Lie splittings and
  // neither is more accurate at this order, so the order is free to be chosen for a structural
  // reason -- and there is one. `advanceClosedReactor` consumes a `ReactorState` holding a
  // `MassFractionState`, whose constructor enforces P18.1's `4*eps*N_species` closure. A
  // transported composition cannot meet that bound: its closure is the linear solve's, eleven
  // orders coarser. Chemistry-first puts the validated state at the STEP BOUNDARY, which is
  // precisely where SpeciesTransport.hpp says the invariant belongs -- "an iterate is a working
  // vector" -- and the transport half then consumes raw fields through the raw-fields overloads
  // that exist for exactly this. The alternative, normalising the transported composition so the
  // constructor would accept it, is the silent renormalisation TD-002 forbids, and it would also
  // make C4b vacuous by driving the measured deviation to eps regardless of the solver tolerance.
  //
  // `advanceClosedReactor` is the CONSTANT-DENSITY reactor, and that is the right closure here
  // precisely because this is the incompressible path: the flow above was solved at the fixed
  // `setup.fluid` density, so a constant-density chemistry step is consistent with it. It would be
  // the wrong closure for a constant-pressure flame -- that is TD-004, not this.
  //
  // TD-002 TURNED THIS INTO A MARCH of `reacting.steps` such steps.
  //
  // TD-001 could only take ONE, and recorded why: step 2's chemistry half would have to start from
  // step 1's TRANSPORTED composition, which closes only to the linear solver's tolerance (measured
  // 5.718198e-08) and so cannot build a `MassFractionState` at all. TD-002 added
  // `integrateChemistryFromFields`, which carries that closure instead of refusing it or erasing
  // it, and the per-species round-off carry that keeps `sum_i Y_i` inside P18.1's invariant over a
  // march long enough to be useful -- measured 0.00 eps over 85 472 sub-steps, against a plain
  // march that is REFUSED after ~1 333.
  //
  // `steps = 1` REDUCES TO TD-001'S SINGLE ADVANCE, BITWISE, and that is a property rather than a
  // special case: when the requested dt is inside the chemical limit the integrator takes exactly
  // one sub-step, whose arithmetic is `advanceClosedReactor` with a zero carry; and when the
  // composition closes exactly, the raw-fields wrapper's shift is exactly zero and adds nothing.
  // Gate criterion C11 measures that against TD-001's recorded figures rather than assuming it.
  //
  // Still NOT a flame (TD-004) and NOT an implicit integrator: the audit found the defect to be
  // round-off accumulation rather than stiffness, and `d(omega_i)/d(Y_j)` still does not exist.
  std::optional<ReactingRunResult> reactingRun;
  std::vector<cfd::io::NamedScalarField> reactingForExport;
  if (setup.reacting.has_value() && massFlux.has_value() && setup.thermal.has_value() &&
      setup.temperatureBoundaries.has_value()) {
    const cfd::io::ReactingSetup& reacting = *setup.reacting;
    const cfd::Index cells = setup.mesh.numberOfCells();
    const cfd::Index speciesCount = reacting.mechanism.speciesCount();
    const cfd::Real dt = reacting.timeStep;

    ReactingRunResult run;

    // Density comes from `setup.fluid`, not from a mixture EOS, because this path is the
    // incompressible one -- a composition-dependent density here would silently change the flow the
    // mass flux above was computed from.
    const std::vector<cfd::fields::ScalarField>& initialFields =
        reacting.initialComposition.fields();
    const cfd::fields::ScalarField density(cells, setup.fluid.density());

    // =========================================================================================
    // THE MARCH -- `reacting.steps` operator-split steps of size dt.
    // =========================================================================================
    //
    // Each step is: chemistry over dt (local, sub-stepped, compensated), then transport of the
    // resulting composition and temperature over the same dt. The order is chemistry-first for the
    // reason TD-001 recorded -- the validated state sits at the step boundary, which is where
    // SpeciesTransport.hpp says the invariant belongs.
    cfd::algebra::LinearSolverSettings reactingLinearSettings;
    reactingLinearSettings.maxIterations = 2000;
    // Stated rather than inherited from LinearSolverSettings' default, which is this same 1e-10,
    // for two reasons: a tolerance is a numerical constant and CLAUDE.md requires those to be
    // visible; and TD-001 gate criterion C4b is a SCALING test on exactly this number, so the
    // quantity it varies has to be identifiable in the source. Behaviour is unchanged.
    reactingLinearSettings.relativeTolerance = 1.0e-10;
    const cfd::algebra::BiCGSTAB reactingLinearSolver{reactingLinearSettings};

    cfd::species::SpeciesDiffusivities reactingDiffusivities;
    reactingDiffusivities.gamma.reserve(static_cast<std::size_t>(speciesCount));
    for (cfd::Index i = 0; i < speciesCount; ++i) {
      // Gamma_i = rho * D_i, which is what the assembler consumes (SpeciesTransport.hpp is explicit
      // that the diffusivity passed in is rho*D_i and not D_i).
      reactingDiffusivities.gamma.emplace_back(
          cells,
          setup.fluid.density() * reacting.transport[static_cast<std::size_t>(i)].diffusivity());
    }

    std::vector<const cfd::boundary::BoundaryConditionSet*> reactingBoundaryPointers;
    reactingBoundaryPointers.reserve(static_cast<std::size_t>(speciesCount));
    for (cfd::Index i = 0; i < speciesCount; ++i) {
      reactingBoundaryPointers.push_back(
          &reacting.compositionBoundaries[static_cast<std::size_t>(i)]);
    }

    const cfd::species::SpeciesSources zeroSources =
        cfd::species::zeroSpeciesSources(speciesCount, cells);
    const cfd::fields::ScalarField reactingConductivity(cells, setup.thermal->conductivity());
    const cfd::fields::ScalarField reactingSpecificHeat(cells, setup.thermal->specificHeat());

    // The marched state. `composition` and `temperature` are RAW fields from step 2 onward, because
    // transport closes only to the linear solve's tolerance -- which is exactly why the chemistry
    // half uses the raw-fields entry point rather than a validated state.
    std::vector<cfd::fields::ScalarField> composition = initialFields;
    cfd::fields::ScalarField temperature = reacting.initialTemperature;
    cfd::fields::ScalarField heatRelease(cells);
    bool reactingSolveFailed = false;
    std::vector<cfd::species::SpeciesResult> lastSpeciesResults;
    cfd::algebra::SolverResult lastTemperatureSolve{};

    for (cfd::Index step = 0; step < reacting.steps; ++step) {
      // ---- chemistry half -----------------------------------------------------------------
      //
      // The rates are evaluated here for the REPORTED diagnostics (max|omega|, max S_E) and the
      // integrator recomputes them internally at every sub-step. Both evaluations see the same
      // state, so they agree; what is reported is the rate at the START of the step, and it is
      // labelled that way rather than presented as a step average.
      //
      // ZeroRateWhereNegative, not Refuse: a transported composition can carry a round-off-negative
      // mass fraction, and P18.5's own comment calls this the CONTINUOUS EXTENSION of the rate law
      // rather than a clip -- every reaction order here is positive, so a negative concentration
      // gives a rate that tends to zero anyway.
      const cfd::species::ReactionProgressRates rates =
          cfd::species::arrheniusProgressRatesFromFields(
              reacting.mechanism, reacting.kinetics, reacting.mixture, composition, density,
              temperature, cfd::species::NegativeConcentrationPolicy::ZeroRateWhereNegative);
      const cfd::species::SpeciesSources sources =
          cfd::species::reactionSpeciesSources(reacting.mechanism, reacting.mixture, rates, cells);
      heatRelease = cfd::species::reactionHeatRelease(reacting.mechanism, reacting.mixture,
                                                      reacting.formation, rates, cells);

      for (cfd::Index i = 0; i < speciesCount; ++i) {
        for (cfd::Index c = 0; c < cells; ++c) {
          run.maxAbsoluteReactionRate =
              std::max(run.maxAbsoluteReactionRate,
                       std::abs(sources.perSpecies[static_cast<std::size_t>(i)].Su[c]));
        }
      }
      for (cfd::Index c = 0; c < cells; ++c) {
        run.maxHeatRelease = std::max(run.maxHeatRelease, heatRelease[c]);
      }

      std::vector<cfd::fields::ScalarField> afterChemistry = composition;
      cfd::fields::ScalarField temperatureAfterChemistry = temperature;
      try {
        const cfd::species::RawFieldsChemistryResult chemistry =
            cfd::species::integrateChemistryFromFields(
                reacting.mechanism, reacting.mixture, reacting.formation, reacting.kinetics,
                reacting.registry, composition, temperature, density, dt);
        afterChemistry = chemistry.composition;
        temperatureAfterChemistry = chemistry.temperature;
        run.chemistrySubSteps += chemistry.subSteps;
        run.chemistryRefusals += chemistry.refusals;
        run.maxChemistryInputSumDeviation =
            std::max(run.maxChemistryInputSumDeviation, chemistry.worstSumDeviationIn);
      } catch (const cfd::Error& refusal) {
        // Preserved, never repaired: the composition stays where it was, the refusal is reported,
        // and the march STOPS rather than continuing from a state the chemistry rejected.
        run.chemistryRefusal = refusal.what();
        reactingSolveFailed = true;
        break;
      }

      // ---- transport half, carrying NO reaction source --------------------------------------
      cfd::species::SpeciesTimeState reactingTime;
      reactingTime.transient = true;
      reactingTime.dt = dt;
      reactingTime.fieldsOld = &afterChemistry;
      reactingTime.rhoOld = &density;
      reactingTime.rhoNew = &density;

      const cfd::species::SpeciesDiffusiveFluxes reactingClosure =
          cfd::species::speciesDiffusiveFaceFluxes(setup.mesh, reacting.registry, afterChemistry,
                                                   reactingDiffusivities, reactingBoundaryPointers);

      std::vector<cfd::fields::ScalarField> transported;
      transported.reserve(static_cast<std::size_t>(speciesCount));
      lastSpeciesResults.clear();
      for (cfd::Index i = 0; i < speciesCount; ++i) {
        const cfd::transport::ScalarTransportAssembly assembly =
            cfd::species::speciesTransportAssembly(
                setup.mesh, i, reacting.registry, afterChemistry, *massFlux, reactingDiffusivities,
                reacting.compositionBoundaries[static_cast<std::size_t>(i)],
                zeroSources.perSpecies[static_cast<std::size_t>(i)], reactingTime,
                &reactingClosure);

        cfd::algebra::Vector guess(static_cast<std::size_t>(cells));
        for (cfd::Index c = 0; c < cells; ++c) {
          guess[c] = afterChemistry[static_cast<std::size_t>(i)][c];
        }
        const cfd::algebra::SolverResult linear =
            reactingLinearSolver.solve(assembly.system, guess);

        // C12 / TD-003: the status is CHECKED. An unchecked status is how a solver failure becomes
        // a physics result -- P18.7 measured BiCGSTAB returning -3.05e12 while reporting Breakdown.
        cfd::fields::ScalarField solved(cells);
        if (linear.converged()) {
          for (cfd::Index c = 0; c < cells; ++c) solved[c] = linear.solution[c];
        } else {
          reactingSolveFailed = true;
          solved = afterChemistry[static_cast<std::size_t>(i)];
        }
        transported.push_back(solved);

        cfd::species::SpeciesResult speciesResult;
        speciesResult.concentration = solved;
        speciesResult.status = linear.converged() ? cfd::species::SpeciesStatus::Converged
                                                  : cfd::species::SpeciesStatus::LinearSolveFailure;
        speciesResult.iterations = linear.iterations;
        speciesResult.finalResidual = linear.finalResidual;
        lastSpeciesResults.push_back(speciesResult);
      }

      // --- the temperature half of the same split.
      //
      // P15's TRANSIENT energy assembly, not `ThermalSolver`: ThermalSolver solves the STEADY
      // energy equation, and a steady temperature solve inside an operator-split step would advance
      // the composition by dt while advancing the temperature to infinity. The heat release is NOT
      // passed here -- the chemistry half already applied it through the reactor's own energy
      // balance, and passing it again would double-count it.
      cfd::thermal::ThermalTimeState reactingThermalTime;
      reactingThermalTime.transient = true;
      reactingThermalTime.dt = dt;
      reactingThermalTime.temperatureOld = &temperatureAfterChemistry;
      reactingThermalTime.densityOld = &density;
      reactingThermalTime.specificHeatOld = &reactingSpecificHeat;
      reactingThermalTime.densityNew = &density;
      reactingThermalTime.specificHeatNew = &reactingSpecificHeat;

      const cfd::thermal::EnergyAssembly energyAssembly = cfd::thermal::assembleEnergyEquation(
          setup.mesh, temperatureAfterChemistry, *massFlux, reactingConductivity,
          reactingSpecificHeat, *setup.temperatureBoundaries, /*volumetricHeatSource=*/0.0,
          cfd::pressure_velocity::nonOrthogonalOptions(setup.solverSettings), reactingThermalTime);

      cfd::algebra::Vector temperatureGuess(static_cast<std::size_t>(cells));
      for (cfd::Index c = 0; c < cells; ++c) temperatureGuess[c] = temperatureAfterChemistry[c];
      lastTemperatureSolve = reactingLinearSolver.solve(energyAssembly.system, temperatureGuess);

      cfd::fields::ScalarField nextTemperature = temperatureAfterChemistry;
      if (lastTemperatureSolve.converged()) {
        for (cfd::Index c = 0; c < cells; ++c) {
          nextTemperature[c] = lastTemperatureSolve.solution[c];
        }
      } else {
        reactingSolveFailed = true;
      }

      composition = std::move(transported);
      temperature = std::move(nextTemperature);
      ++run.stepsTaken;
    }

    const std::vector<cfd::fields::ScalarField>& finalComposition = composition;
    const cfd::fields::ScalarField& finalTemperature = temperature;

    for (cfd::Index i = 0; i < speciesCount; ++i) {
      const cfd::species::SpeciesResult speciesResult =
          static_cast<std::size_t>(i) < lastSpeciesResults.size()
              ? lastSpeciesResults[static_cast<std::size_t>(i)]
              : cfd::species::SpeciesResult{};
      run.speciesResults.push_back(SpeciesRunResult{reacting.registry.name(i), speciesResult});
      reactingForExport.emplace_back("mass_fraction_" + reacting.registry.name(i),
                                     finalComposition[static_cast<std::size_t>(i)]);

      // The REACTING species reach metadata.json through the same list the passive ones use.
      // Without this a reacting run exported its mass-fraction fields while metadata.json still
      // said `"species": []`, which is the same "configured but not reported" shape TD-001 exists
      // to remove -- a reader would conclude no species were solved. Measured before the fix.
      speciesMetadataForExport.push_back(cfd::io::SpeciesRunMetadata{
          reacting.registry.name(i), reacting.transport[static_cast<std::size_t>(i)].diffusivity(),
          speciesStatusName(speciesResult.status), speciesResult.converged(),
          speciesResult.iterations, speciesResult.finalResidual});
    }

    run.temperatureSolveConverged = lastTemperatureSolve.converged();
    run.temperatureIterations = lastTemperatureSolve.iterations;
    run.temperatureResidual = lastTemperatureSolve.finalResidual;
    run.anyLinearSolveFailed = reactingSolveFailed;

    // --- measured evidence that chemistry and the energy coupling actually ran. Every quantity is
    // taken against the step's STARTING state, which is the case file's declared initial condition.
    for (cfd::Index c = 0; c < cells; ++c) {
      cfd::Real sum = 0.0;
      for (cfd::Index i = 0; i < speciesCount; ++i) {
        const cfd::Real before = initialFields[static_cast<std::size_t>(i)][c];
        const cfd::Real after = finalComposition[static_cast<std::size_t>(i)][c];
        run.maxCompositionChange = std::max(run.maxCompositionChange, std::abs(after - before));
        run.minMassFraction = std::min(run.minMassFraction, after);
        sum += after;
      }
      run.maxCompositionSumDeviation =
          std::max(run.maxCompositionSumDeviation, std::abs(sum - 1.0));
      run.maxTemperatureChange = std::max(
          run.maxTemperatureChange, std::abs(finalTemperature[c] - reacting.initialTemperature[c]));
    }

    reactingForExport.emplace_back("temperature_reacting", finalTemperature);
    reactingForExport.emplace_back("heat_release", heatRelease);

    reactingRun = std::move(run);
    out.reactingResult = reactingRun;

    // TD-047: the reacting march's own linear-solve outcome reaches the run status, and
    // therefore exitCodeFor(), here. Before this, `out.status` was whatever the FLOW solve
    // produced at the top of this function and the march could not change it -- so a reacting
    // case whose every species and energy solve FAILED exited 0. This read is also the only
    // consumer `anyLinearSolveFailed` has; it was written and never read.
    out.status = statusWithReactingOutcome(out.status, out.reactingResult->anyLinearSolveFailed);
  }

  // P6-PHYS-002: same one-way, best-available policy as thermal/species
  // above. One VolumeFractionSolver::step() call (see
  // ProjectRunner.hpp's own header comment: this is the foundation's own
  // "single implicit-Euler step, no outer loop" scope, not an
  // approximation invented here), then mixture density/viscosity fields
  // evaluated at the *final* (post-step) alpha for reporting/export --
  // never fed back into this same SIMPLE solve (which already ran with
  // mu_mix at the *initial* alpha, above).
  std::optional<cfd::io::MultiphaseRunMetadata> multiphaseMetadata;
  std::vector<cfd::io::NamedScalarField> multiphaseForExport;
  if (setup.multiphase.has_value()) {
    const auto& m = *setup.multiphase;
    const auto& p1 = m.system.phase1();
    const auto& p2 = m.system.phase2();
    if (massFlux.has_value()) {
      const cfd::multiphase::VolumeFractionSolver alphaSolver{};
      const cfd::multiphase::VolumeFractionStepResult alphaStep = alphaSolver.step(
          setup.mesh, m.initialAlpha, *massFlux, m.alphaBoundaries, m.transportTimeStep);
      cfd::fields::ScalarField mixtureDensity =
          cfd::multiphase::evaluateMixtureDensityField(setup.mesh, alphaStep.alpha, m.system);
      cfd::fields::ScalarField mixtureViscosity =
          cfd::multiphase::evaluateMixtureViscosityField(setup.mesh, alphaStep.alpha, m.system);
      const Real phase1Volume = cfd::multiphase::phaseVolume(setup.mesh, alphaStep.alpha);

      multiphaseMetadata =
          cfd::io::MultiphaseRunMetadata{p1.name(),
                                         p2.name(),
                                         p1.density(),
                                         p1.viscosity(),
                                         p2.density(),
                                         p2.viscosity(),
                                         volumeFractionStatusName(alphaStep.status),
                                         alphaStep.converged(),
                                         phase1Volume};
      multiphaseForExport.emplace_back("volume_fraction", alphaStep.alpha);
      multiphaseForExport.emplace_back("mixture_density", mixtureDensity);
      multiphaseForExport.emplace_back("mixture_viscosity", mixtureViscosity);
      out.multiphaseResult = MultiphaseRunResult{alphaStep, std::move(mixtureDensity),
                                                 std::move(mixtureViscosity), phase1Volume};
    } else {
      multiphaseMetadata = cfd::io::MultiphaseRunMetadata{
          p1.name(), p2.name(), p1.density(), p1.viscosity(), p2.density(), p2.viscosity(),
          "NotRun",  false,     0.0};
    }
  }

  // P6-PHYS-003 (extended by P12-COMP-002): two modes, see
  // ProjectRunner.hpp's own header comment. Runs whenever the SIMPLE
  // result is fully finite -- same gate as massFlux above (compressible
  // needs velocity, not massFlux itself, but shares the same "only a
  // usable flow" precondition).
  std::optional<cfd::io::CompressibleRunMetadata> compressibleMetadata;
  std::vector<cfd::io::NamedScalarField> compressibleForExport;
  if (setup.compressible.has_value()) {
    const auto& c = *setup.compressible;
    // thermal_coupled requires "thermal" (PhysicsConfigParser.cpp's own
    // cross-block check), and both compressible and thermal share the
    // identical `!anyNonFinite(result)` gate above, so thermalResult is
    // guaranteed to have a value here whenever c.thermalCoupled is true
    // and this branch is reached -- checked anyway (defensive, not load-
    // bearing) rather than assumed.
    const bool canEvaluate =
        !anyNonFinite(result) && (!c.thermalCoupled || thermalResult.has_value());
    if (canEvaluate) {
      const cfd::Index n = setup.mesh.numberOfCells();
      cfd::fields::ScalarField temperatureField(n);
      if (c.thermalCoupled) {
        for (cfd::Index i = 0; i < n; ++i) temperatureField[i] = thermalResult->temperature[i];
      } else {
        for (cfd::Index i = 0; i < n; ++i) temperatureField[i] = *c.temperature;
      }
      // P12-COMP-001: boundary faces now get a real EOS-evaluated density
      // at their own boundary pressure/temperature (via
      // setup.pressureBoundaries and, when thermal-coupled,
      // *setup.temperatureBoundaries), superseding the previous owner-
      // cell-reuse simplification -- see CompressibleMassFlux.hpp's own
      // header comment. Shared by both modes below.
      const cfd::boundary::BoundaryConditionSet* temperatureBoundariesPtr =
          c.thermalCoupled ? &(*setup.temperatureBoundaries) : nullptr;

      cfd::fields::ScalarField density(n);
      cfd::fields::ScalarField pressureAbsolute(n);
      cfd::fields::ScalarField mach(n);
      Real machMax = 0.0;
      cfd::fields::SurfaceField compressibleMassFlux(setup.mesh.numberOfFaces());
      std::string statusName;

      if (c.coupled) {
        // P12-COMP-002: genuinely coupled solve, warm-started from the
        // incompressible SIMPLE result already computed above -- see
        // CompressibleSIMPLE.hpp's own header comment. Initial density
        // guess is the EOS evaluated at the warm-start's own gauge
        // pressure (a reasonable starting iterate, not itself part of the
        // converged answer).
        for (cfd::Index i = 0; i < n; ++i)
          pressureAbsolute[i] = c.referencePressure + result.pressure[i];
        const cfd::fields::ScalarField initialDensity = cfd::compressible::evaluateDensityField(
            setup.mesh, pressureAbsolute, temperatureField, c.thermodynamics);

        cfd::compressible::CompressibleSIMPLESettings compressibleSettings;
        compressibleSettings.maxIterations = setup.solverSettings.maxIterations;
        compressibleSettings.velocityRelaxation = setup.solverSettings.velocityRelaxation;
        compressibleSettings.pressureRelaxation = setup.solverSettings.pressureRelaxation;
        compressibleSettings.velocityTolerance = setup.solverSettings.velocityTolerance;
        compressibleSettings.pressureTolerance = setup.solverSettings.pressureTolerance;
        compressibleSettings.continuityTolerance = setup.solverSettings.continuityTolerance;
        compressibleSettings.momentumSolver = setup.solverSettings.momentumSolver;
        compressibleSettings.pressureSolver = setup.solverSettings.pressureSolver;
        // P12-NUM-003: same non-orthogonal-correction switch as SIMPLE.
        compressibleSettings.nonOrthogonalCorrections =
            setup.solverSettings.nonOrthogonalCorrections;
        compressibleSettings.gradientScheme = setup.solverSettings.gradientScheme;
        // P12-NUM-004: the same solver.json robustness settings.
        compressibleSettings.robustness = setup.solverSettings.robustness;
        // compressibleSettings.pseudoTimeStep is left at its own default
        // (1.0) -- no case-format exposure for it in this phase (see
        // ROADMAP.md's P12-COMP-002 scope note).

        const cfd::compressible::CompressibleSIMPLE compressibleSimple(
            compressibleSettings, c.thermodynamics, c.referencePressure, /*referenceCell=*/0);
        cfd::compressible::CompressibleSIMPLEResult coupled = compressibleSimple.solve(
            setup.mesh, setup.fluid.dynamicViscosity(), setup.velocityBoundaries,
            setup.pressureBoundaries, temperatureField, temperatureBoundariesPtr, result.velocity,
            result.pressure, initialDensity);

        density = coupled.density;
        for (cfd::Index i = 0; i < n; ++i)
          pressureAbsolute[i] = c.referencePressure + coupled.pressure[i];
        for (cfd::Index i = 0; i < n; ++i) {
          const Real speed = magnitude(coupled.velocity[i]);
          const Real soundSpeed = c.thermodynamics.speedOfSound(temperatureField[i]);
          mach[i] = cfd::compressible::machNumber(speed, soundSpeed);
          machMax = std::max(machMax, mach[i]);
        }
        compressibleMassFlux = coupled.massFlux;
        statusName = compressibleSimpleStatusName(coupled.status);
        // The coupled solve's own convergence is this run's authoritative
        // status -- see ProjectRunner.hpp's own header comment on
        // compressibleSimpleResult.
        out.status = statusFor(coupled.status);
        out.compressibleSimpleResult = std::move(coupled);
      } else {
        // The post-hoc pass: a *reinterpretation* of the already-converged
        // incompressible SIMPLE result, never a second flow solve -- see
        // ProjectRunner.hpp's own header comment for the exact recipe.
        for (cfd::Index i = 0; i < n; ++i)
          pressureAbsolute[i] = c.referencePressure + result.pressure[i];

        density = cfd::compressible::evaluateDensityField(setup.mesh, pressureAbsolute,
                                                          temperatureField, c.thermodynamics);

        for (cfd::Index i = 0; i < n; ++i) {
          const Real speed = magnitude(result.velocity[i]);
          const Real soundSpeed = c.thermodynamics.speedOfSound(temperatureField[i]);
          mach[i] = cfd::compressible::machNumber(speed, soundSpeed);
          machMax = std::max(machMax, mach[i]);
        }

        compressibleMassFlux = cfd::compressible::calculateCompressibleMassFlux(
            setup.mesh, result.velocity, density, setup.velocityBoundaries, result.pressure,
            setup.pressureBoundaries, c.referencePressure, c.thermodynamics, temperatureField,
            temperatureBoundariesPtr);
        statusName = "Evaluated";
      }

      const cfd::physics::ContinuityResult continuity =
          cfd::physics::evaluateContinuity(setup.mesh, compressibleMassFlux);

      compressibleMetadata =
          cfd::io::CompressibleRunMetadata{c.thermodynamics.gasConstant(),
                                           c.thermodynamics.specificHeatPressure(),
                                           c.referencePressure,
                                           c.thermalCoupled,
                                           c.coupled,
                                           statusName,
                                           machMax,
                                           std::abs(continuity.globalNetFlux)};
      compressibleForExport.emplace_back("density", density);
      compressibleForExport.emplace_back("pressure_absolute", pressureAbsolute);
      compressibleForExport.emplace_back("compressible_temperature", temperatureField);
      compressibleForExport.emplace_back("mach_number", mach);
      out.compressibleResult = CompressibleRunResult{std::move(density),
                                                     std::move(pressureAbsolute),
                                                     std::move(temperatureField),
                                                     std::move(mach),
                                                     compressibleMassFlux,
                                                     continuity,
                                                     machMax};
    } else {
      compressibleMetadata =
          cfd::io::CompressibleRunMetadata{c.thermodynamics.gasConstant(),
                                           c.thermodynamics.specificHeatPressure(),
                                           c.referencePressure,
                                           c.thermalCoupled,
                                           c.coupled,
                                           "NotRun",
                                           0.0,
                                           0.0};
    }
  }

  // P13-004: the exported metadata now carries how the run was solved, not
  // just that it was. ProjectRunner is the one layer holding both the case
  // definition and the result, which is why it fills this rather than
  // JSONWriter reaching into the case pipeline.
  cfd::io::RunMetadata exportMetadata{
      caseDefinition->caseConfig.name, caseDefinition->physics.density,
      caseDefinition->physics.dynamicViscosity, caseDefinition->solver.type};
  exportMetadata.convectionScheme = caseDefinition->solver.convectionScheme;
  exportMetadata.gradientScheme = caseDefinition->solver.gradientScheme;
  exportMetadata.nonOrthogonalCorrections = caseDefinition->solver.nonOrthogonalCorrections;
  exportMetadata.linearSolverBackend = caseDefinition->solver.pressureSolver.backend;
  exportMetadata.velocityTolerance = caseDefinition->solver.velocityTolerance;
  exportMetadata.pressureTolerance = caseDefinition->solver.pressureTolerance;
  exportMetadata.continuityTolerance = caseDefinition->solver.continuityTolerance;
  exportMetadata.maxIterations = caseDefinition->solver.maxIterations;
  try {
    const std::optional<cfd::fields::ScalarField> temperatureForExport =
        thermalResult.has_value() ? std::optional(thermalResult->temperature) : std::nullopt;
    // P6-PHYS-001 (generalized by P6-PHYS-002/003): every species/
    // multiphase/compressible field combined into one flat list -- see
    // ResultExporter.hpp's own header comment on why this is one
    // parameter, not one per physics module.
    std::vector<cfd::io::NamedScalarField> extraFieldsForExport = std::move(speciesForExport);
    extraFieldsForExport.insert(extraFieldsForExport.end(),
                                std::make_move_iterator(multiphaseForExport.begin()),
                                std::make_move_iterator(multiphaseForExport.end()));
    extraFieldsForExport.insert(extraFieldsForExport.end(),
                                std::make_move_iterator(compressibleForExport.begin()),
                                std::make_move_iterator(compressibleForExport.end()));
    // TD-001 / gate C8: the reacting fields reach the exported artefacts through the SAME
    // extra-fields channel every other optional physics block uses -- `mass_fraction_<name>` per
    // species, `temperature_reacting`, and `heat_release` as the reaction quantity.
    extraFieldsForExport.insert(extraFieldsForExport.end(),
                                std::make_move_iterator(reactingForExport.begin()),
                                std::make_move_iterator(reactingForExport.end()));
    out.exportSummary = cfd::io::ResultExporter::write(
        caseDirectory / "results", setup.mesh, result, exportMetadata, temperatureForExport,
        thermalMetadata, extraFieldsForExport, speciesMetadataForExport, multiphaseMetadata,
        compressibleMetadata, setup.meshQuality);
  } catch (const cfd::Error& e) {
    // P13-005 (L7), amendment A1: Debug for the same reason as L1 -- the CLI
    // prints "Run error: <this same text>" on stderr already.
    cfd::Logger::instance().debug(std::string("export failed: ") + e.what());
    out.status = ProjectRunStatus::ApplicationError;
    out.errorMessage = e.what();
  }
  return out;
}

}  // namespace cfd::app
