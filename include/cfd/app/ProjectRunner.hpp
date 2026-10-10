#pragma once

// P5 -- Application, section 0's central architectural rule: "ONE SOLVER
// BACKEND", shared by the CLI, the GUI, the case manager, and any future
// automation client. ProjectRunner is that one backend entry point --
// the exact orchestration apps/cli/main.cpp's own runCase() used to
// inline directly (CaseReader -> CaseBuilder -> turbulence-model
// construction -> SIMPLE::solve() -> optional thermal solve ->
// ResultExporter::write()) now lives here instead, unchanged in
// behavior (tests/integration/case/test_project_runner_cli_equivalence.cpp
// checks the CLI's own printed report/exit code are unaffected), so the
// GUI's SimulationController (apps/gui/) calls the exact same function
// the CLI calls -- never a second, GUI-only solve path (section 9's
// "CLI/GUI execution equivalence" requirement is satisfied by
// construction, not by comparison after the fact).
//
// Steady-incompressible-SIMPLE(+thermal+species+multiphase+compressible)
// scope, exactly matching what CaseReader/CaseBuilder support today.
// Species (P6-PHYS-001) followed thermal's own precedent: one-way,
// best-available, run only when the SIMPLE result is fully finite (see
// this file's own "same one-way, best-available ... solve" comment in
// ProjectRunner.cpp), never coupled back into the momentum equation
// (this codebase's species are passive/non-reacting, so there is no
// physical density/viscosity feedback to couple).
//
// Multiphase (P6-PHYS-002): mu_mix(alpha), evaluated at the case's
// configured *initial* alpha and held fixed for the entire SIMPLE solve
// (the same "held fixed across this entire solve() call" one-way
// convention P3-PHYS-001's buoyancy/temperature coupling already
// established), is fed into SIMPLE's momentum assembly through its
// existing turbulenceModel effective-viscosity injection point (see
// cfd::turbulence::TurbulenceModel.hpp's own header comment: mu_eff =
// mu + mu_t is generic, not turbulence-specific by construction) via a
// small adapter in ProjectRunner.cpp -- reusing this project's existing
// architecture rather than adding a second momentum-assembly path.
// rho_mix is deliberately NOT coupled into continuity/pressure
// correction (cfd::multiphase::MultiphaseProperties.hpp's own explicit
// scope disclosure: "evaluated and validated standalone only") -- SIMPLE
// still solves with the case's single, constant, top-level physics.json
// density. After SIMPLE converges, alpha is advanced exactly one
// implicit-Euler step (cfd::multiphase::VolumeFractionSolver::step(),
// which is not an outer-iterated solve by its own design -- see its own
// header comment) using the converged massFlux, then mixture density/
// viscosity fields are evaluated at that *final* alpha for reporting/
// export. Configured together with "turbulence" is rejected at parse
// time (PhysicsConfigParser.cpp) -- both would need the same one
// effective-viscosity injection point.
//
// Compressible (P6-PHYS-003, extended by P12-COMP-002): two modes,
// chosen by physics.json's "compressible.coupled" (default false).
//
//   coupled=false (default): a *post-hoc* reinterpretation of the
//   already-converged incompressible SIMPLE result -- exactly
//   tests/integration/compressible/test_low_mach_regression.cpp's own
//   validated recipe (absolute pressure -> IdealGasEOS density -> Mach
//   number -> compressible mass flux -> steady continuity imbalance),
//   never a second, parallel flow solve. Energy coupling is either
//   isothermal (a configured constant temperature) or, if
//   "thermal_coupled" is set, the case's own separately-configured
//   "thermal" block's converged temperature field -- both read-only
//   inputs to the EOS, not fed back into momentum/continuity.
//
//   coupled=true: dispatches to cfd::compressible::CompressibleSIMPLE, a
//   genuinely coupled compressible pressure-velocity solver (density is
//   iterated state, updated via the EOS from the corrected pressure
//   every outer iteration -- see that class's own header comment),
//   warm-started from the incompressible SIMPLE result computed above.
//   This solve's own status becomes the run's authoritative status (see
//   ProjectRunResult::compressibleSimpleResult). Disclosed, deliberate
//   scope limitation of this first coupled implementation: no
//   turbulence-model or buoyancy-source injection point exists yet, so
//   "turbulence"/"buoyancy" combined with "coupled": true are rejected
//   at parse time (PhysicsConfigParser.cpp) rather than silently
//   ignored -- the non-coupled mode is unaffected and still allows both.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/io/ResultExporter.hpp"
#include "cfd/io/case/CaseDefinition.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/multiphase/VolumeFractionSolver.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/pressure_velocity/SIMPLEProgress.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"
#include "cfd/species/SpeciesSolver.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

namespace cfd::app {

// Mirrors apps/cli/main.cpp's own pre-P5 ExitCode enum one-for-one
// (kExitConverged=0 ... kExitNumericalFailure=4), plus the one new
// category P5 itself introduces (Cancelled, exit code 5) -- see
// exitCodeFor() in apps/cli/main.cpp, now implemented once here and
// simply forwarded to by both the CLI and the GUI/case-manager layer.
enum class ProjectRunStatus {
  Converged,
  DidNotConverge,
  NumericalFailure,
  Cancelled,
  InvalidCase,
  ApplicationError,
};

[[nodiscard]] int exitCodeFor(ProjectRunStatus status) noexcept;

// P6-PHYS-001: one physics.json-declared species's solve outcome, in
// declaration order -- `name` lets a caller (CLI report, GUI, export)
// identify which species a given cfd::species::SpeciesResult belongs to
// without re-deriving it from `caseDefinition->physics.species` by index.
struct SpeciesRunResult {
  std::string name;
  cfd::species::SpeciesResult result;
};

// TD-001: the reacting production-run outcome.
//
// This is the object that closes the CLAUDE.md section 9 gap for the reacting stack: before it,
// `ProjectRunner` had NO reference to `setup.reacting` at all (measured: 0), so a case file could
// declare a mechanism and get a full result set with `species: []` and no reaction quantity
// anywhere -- see results/technical_debt/TD001_reacting_production/reproducer.md variant 3.
//
// SCOPE, stated because it is narrow on purpose. This is **one operator-split time step** of size
// `reacting.time_step`, in the same "best-available" sense every other optional physics block in
// ProjectRunner already uses (ProjectRunner.cpp's own policy comment): chemistry over dt on the
// case's validated initial state, then transport of the resulting composition and temperature over
// the same dt, driven by the converged flow's mass flux. It is NOT a converged reacting flame and
// NOT a time-accurate multi-step march -- TD-004 owns the flame, TD-002 owns the stiff integrator.
//
// WHY ONE STEP AND NOT A MARCH. A second step's chemistry half would have to start from the first
// step's TRANSPORTED composition, and `advanceClosedReactor` consumes a `ReactorState` holding a
// `MassFractionState`, whose constructor enforces P18.1's `4*eps*N_species` closure. An iteratively
// transported composition cannot meet that bound -- its closure is the linear solve's -- so a march
// needs a raw-fields reactor entry point, the same way the iterate path needed
// `arrheniusProgressRatesFromFields`. That entry point belongs to TD-002, which owns the
// integrator; normalising the composition to fit the constructor instead would be exactly the
// silent renormalisation TD-002 forbids. See results/technical_debt/TD001_reacting_production/
// implementation.md section 3.
//
// The TD-002 step-budget limitation applies to whatever this ships: a reacting case driven far
// enough will exceed P18.1's `sum Y_i = 1` tolerance, because the chemistry source here is still
// P18's explicit first-order one.
struct ReactingRunResult {
  // Per-species TRANSPORT-half outcome, in species-index order.
  std::vector<SpeciesRunResult> speciesResults;

  // The temperature transport half's own linear solve, reported rather than folded into a status so
  // gate C12 can be checked on it directly.
  bool temperatureSolveConverged{false};
  Index temperatureIterations{};
  Real temperatureResidual{};

  // Measured evidence that the chemistry actually ran, rather than that it was configured.
  Real maxAbsoluteReactionRate{};     // max_cell max_i |omega_i|   [kg/(m^3 s)]
  Real maxHeatRelease{};              // max_cell S_E               [W/m^3]
  Real maxCompositionChange{};        // max_cell max_i |Y_i - Y_i^0|
  Real maxTemperatureChange{};        // max_cell |T - T^0|          [K]
  Real maxCompositionSumDeviation{};  // max_cell |sum_i Y_i - 1|  -- gate C4b/C4d

  // Gate C4c (amendment A1): min_cell min_i Y_i over the FINAL composition, gated at >= 0 with no
  // tolerance. Initialised ABOVE every admissible mass fraction so an empty reduction cannot read
  // as a pass -- the fully explicit variant that A1-N2 preserves reached -0.297 while conserving
  // the sum better than any other treatment, so a gate on the sum alone would have rewarded it.
  Real minMassFraction{2.0};

  // Non-empty exactly when the chemistry half REFUSED (P18.6 refuses rather than clipping, and
  // TD-002's integrator refuses rather than giving up silently). The refusal's own message,
  // preserved, and the march STOPS at that step rather than continuing from a rejected state.
  std::string chemistryRefusal;

  // TD-002: what the march actually did, reported rather than inferred from the case file.
  //
  // `stepsTaken` can be less than `reacting.steps` -- that is the point of reporting it. A refused
  // chemistry half breaks out of the march, and a run that took 3 of 10 requested steps must say
  // so rather than present a 3-step answer as a 10-step one.
  Index stepsTaken{};
  // Summed over the march. These are the numbers TD-002's budget is about: the sub-step count is
  // how many qualified explicit steps the chemistry actually needed, and a non-zero refusal count
  // means the limiter had to retry smaller.
  std::size_t chemistrySubSteps{};
  std::size_t chemistryRefusals{};
  // The worst closure the chemistry half was HANDED, over the march. From step 2 onward its input
  // is a transported composition, so this is the inherited transport error TD-001 root-caused --
  // carried through the chemistry rather than erased by it, which is gate criterion C9.
  Real maxChemistryInputSumDeviation{};

  // TD-001 gate C12 / TD-003: true if ANY per-species linear solve did not converge. Reported
  // rather than swallowed -- P18.7 measured BiCGSTAB returning -3.05e12 while itself reporting
  // Breakdown, so an unchecked status is how a solver failure becomes a physics result.
  bool anyLinearSolveFailed{false};
};

// P6-PHYS-002: the multiphase production-run outcome -- the one-step
// volume-fraction advance plus mixture property fields evaluated at the
// *final* (post-step) alpha (see ProjectRunner.hpp's own header comment
// on the coupling order).
struct MultiphaseRunResult {
  cfd::multiphase::VolumeFractionStepResult alphaStep;
  cfd::fields::ScalarField mixtureDensity;
  cfd::fields::ScalarField mixtureViscosity;
  // multiphase::phaseVolume(mesh, alpha) at the final alpha -- the
  // conservation quantity ProjectRunner reports (see
  // ProjectRunner.cpp's own comment on why this, not a flux-balance
  // check, is the meaningful conservation metric for a single transport
  // step).
  Real phase1Volume{};
};

// P6-PHYS-003: the compressible post-hoc low-Mach outcome -- every field
// this foundation actually computes from the already-converged
// incompressible SIMPLE result (see ProjectRunner.hpp's own header
// comment for the exact recipe).
struct CompressibleRunResult {
  cfd::fields::ScalarField density;           // EOS-evaluated, cell-ordered.
  cfd::fields::ScalarField pressureAbsolute;  // referencePressure + SIMPLE's gauge pressure.
  cfd::fields::ScalarField temperature;       // the constant or thermal-coupled field used.
  cfd::fields::ScalarField machNumber;
  // P12-COMP-001: the actual per-face compressible mass flux (boundary
  // faces now EOS-evaluated at their own boundary pressure/temperature,
  // not the owner cell's density) -- kept, not just consumed internally
  // by `continuity` below, so a caller/test can directly confirm the new
  // boundary-density treatment took effect at a specific face.
  cfd::fields::SurfaceField massFlux;
  cfd::physics::ContinuityResult continuity;  // from the compressible mass flux.
  Real machMax{};
};

// Both default-empty/no-op (SIMPLEProgress.hpp's own contract) -- an
// options value with neither set reproduces the exact prior CLI
// behavior byte-for-byte.
struct ProjectRunOptions {
  cfd::pressure_velocity::SIMPLEProgressCallback progressCallback;
  cfd::pressure_velocity::SIMPLECancellationCheck cancellationCheck;

  // P13-001 -- Restart / checkpointing. All three default to "off", so an
  // options value that sets none of them reproduces the exact prior
  // behaviour, structurally: one solve() call, no checkpoint file touched.
  //
  // resumeFrom: a checkpoint file to continue from. The run then starts from
  // that checkpoint's velocity, pressure and face mass flux instead of the
  // case's initial conditions, and continues its iteration count and residual
  // history rather than restarting them. The checkpoint must match this
  // case's mesh AND its configuration fingerprint (cfd::io::
  // computeCaseFingerprint) -- only solver.max_iterations may differ, so
  // "give it more iterations and finish it" works and nothing else does.
  //
  // checkpointPath: where to write. Written when the run ends and, if
  // checkpointInterval > 0, every that many completed outer iterations,
  // always to this same path (overwritten -- a checkpoint is the state now,
  // not a history of states; the numbered-file convention this project
  // already rejected for results applies here too).
  //
  // checkpointInterval: how the run is SEGMENTED. The solve is executed as
  // consecutive SIMPLE::solve() calls of at most this many outer iterations,
  // each continuing the last through a SIMPLEContinuation. That is exactly
  // what a resume does, so periodic checkpointing exercises the resume path
  // continuously instead of only after a crash -- and, because the
  // continuation is exact, a segmented run produces bitwise the same result
  // as an unsegmented one (results/p13/restart/, group B).
  //
  // The cost of segmenting is one per-solve setup per segment (derived
  // fields and, on the GPU path, the device plans), so an interval of 1 is
  // legal and slow and an interval in the hundreds is what production wants.
  // No transfer is added INSIDE a segment: the device state comes back at
  // the end of a solve() call exactly as it already did.
  std::optional<std::filesystem::path> resumeFrom;
  std::optional<std::filesystem::path> checkpointPath;
  cfd::Index checkpointInterval{0};
};

// Every intermediate artifact a caller might want (the GUI needs the
// case definition and residual history for display; the CLI needs the
// export summary's paths for its own printed report) -- optional,
// populated as far as the run actually got (e.g. caseDefinition is
// always set once the case parses, even if the solve itself later
// fails; simpleResult/thermalResult/exportSummary stay unset for an
// InvalidCase/ApplicationError run that never reached them).
// P16 -- what the turbulent heat-transfer coupling actually used.
//
// Exposed because a coupling whose inputs cannot be inspected cannot be
// verified: the conservation check has to evaluate the wall heat flux with the
// SAME k_eff = k_mol + mu_t cp / Pr_t the solve used, and computing that needs
// both members below. It is also the structured diagnostic for Pr_t.
//
// Present exactly when turbulent heat transfer was active -- i.e. when a real
// turbulence model supplied mu_t. Absent for laminar and for multiphase
// mixture viscosity, which is not turbulence.
struct TurbulentThermalRunResult {
  // The resolved value: the case's turbulence.turbulent_prandtl, or
  // cfd::turbulence::kDefaultTurbulentPrandtl when the case omitted it.
  Real turbulentPrandtl{};
  // The CONVERGED mu_t handed to the thermal solve. A copy, not a reference:
  // the turbulence model does not outlive run().
  cfd::fields::ScalarField turbulentViscosity;
};

struct ProjectRunResult {
  ProjectRunStatus status{ProjectRunStatus::ApplicationError};
  std::optional<cfd::io::CaseDefinition> caseDefinition;
  // P5 GUI visualization integration: the exact Mesh the case built and
  // solved against (CaseBuilder's own deterministic output from
  // caseDefinition) -- set alongside caseDefinition, whenever the case
  // parsed far enough to build one, regardless of whether the solve
  // itself later failed. cfd::viz's mesh-based algorithms (FieldProbe,
  // VectorSampling) need this; MarchingSquares does not (it takes plain
  // coordinate/value arrays -- see its own header comment).
  std::optional<cfd::mesh::Mesh> mesh;
  // P12-MESH-004: the production mesh-quality report of `mesh` (set with it).
  std::optional<cfd::mesh::MeshQualityReport> meshQuality;
  std::optional<cfd::pressure_velocity::SIMPLEResult> simpleResult;
  std::optional<cfd::thermal::ThermalResult> thermalResult;
  std::optional<TurbulentThermalRunResult> turbulentThermalResult;
  // P6-PHYS-001: one entry per physics.json-declared species, in
  // declaration order -- always the full declared set once the SIMPLE
  // result is fully finite (never partially populated the way a single
  // optional field could leave ambiguous which species ran), empty
  // otherwise (mirrors thermalResult staying unset when the case has no
  // thermal block, generalized from "unset" to "empty vector" for a
  // list-shaped field).
  std::vector<SpeciesRunResult> speciesResults;
  // TD-001: present exactly when the case declared a "reacting" block and the flow produced a
  // usable mass flux; unset otherwise, same convention as thermalResult.
  std::optional<ReactingRunResult> reactingResult;
  // P6-PHYS-002/003: present iff the case configured the corresponding
  // block AND the SIMPLE result was fully finite (same "run only once
  // the flow is usable" policy species/thermal already follow) -- unset
  // otherwise, never a partially-populated struct.
  std::optional<MultiphaseRunResult> multiphaseResult;
  std::optional<CompressibleRunResult> compressibleResult;
  // P12-COMP-002: present iff `compressible.coupled` was set and the
  // incompressible warm-start was usable -- the full, authoritative
  // coupled-solver result (its own status/residuals/iterated density),
  // additive to `compressibleResult` above (which is still populated,
  // from the coupled result's own final state, for the export pipeline's
  // existing field names). When this is set, `status` above reflects
  // *this* result's own convergence, not the warm-start SIMPLE run's.
  std::optional<cfd::compressible::CompressibleSIMPLEResult> compressibleSimpleResult;
  std::optional<cfd::io::ResultExportSummary> exportSummary;
  // P13-001: what the checkpointing actually did, reported rather than
  // logged so a test can assert it. `resumedFromIteration` is 0 for a run
  // that did not resume; `checkpointsWritten` counts every write including
  // the final one; `checkpointPath` is set once something was written.
  cfd::Index resumedFromIteration{0};
  cfd::Index checkpointsWritten{0};
  std::optional<std::filesystem::path> checkpointPath;
  // Populated for InvalidCase (CaseReader/CaseBuilder's own message) and
  // ApplicationError (a result-export I/O failure) -- empty otherwise,
  // same "name the offending file/field" convention CaseReader already
  // follows (never a bare "Invalid case").
  std::string errorMessage;
  // P13-002: the same rejection as DATA -- which file, which field, what
  // constraint, what was received. Set for an InvalidCase whose rejection
  // came from the case parsers, which is ~140 of the pipeline's rejection
  // sites; unset for the handful raised directly by CaseBuilder during
  // construction, where there is no single field to name (see
  // results/p13/case-validation/summary.md for that list).
  //
  // `errorMessage` remains the authority for what to SHOW a user. This is for
  // a consumer that needs to act on the rejection -- switching to the right
  // editor page, highlighting the right input -- without parsing prose. The
  // GUI used to recover two of these fields by searching the message text,
  // and renaming one token in that message silently broke it.
  std::optional<cfd::CaseDiagnostic> caseDiagnostic;
};

class ProjectRunner {
 public:
  // Reads, builds, and solves `caseDirectory` exactly the way the CLI's
  // pre-P5 runCase() did, and writes results to `caseDirectory/results`
  // the same way (ResultExporter's own always-write-metadata-and-
  // residuals-even-on-failure policy, TODO.md P1 section 29) -- the one
  // difference from a bare direct call is `options`, threaded straight
  // into SIMPLE's own constructor unchanged.
  [[nodiscard]] static ProjectRunResult run(const std::filesystem::path& caseDirectory,
                                            const ProjectRunOptions& options = {});
};

}  // namespace cfd::app
