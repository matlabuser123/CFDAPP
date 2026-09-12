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
// Compressible (P6-PHYS-003): this foundation has no compressible
// pressure-velocity solver (cfd::compressible::CompressibleContinuity.hpp's
// own header comment: "NOT a separately-iterated compressible pressure-
// correction solve"), so this is a *post-hoc* reinterpretation of the
// already-converged incompressible SIMPLE result -- exactly
// tests/integration/compressible/test_low_mach_regression.cpp's own
// validated recipe (absolute pressure -> IdealGasEOS density -> Mach
// number -> compressible mass flux -> steady continuity imbalance),
// never a second, parallel flow solve. Energy coupling is either
// isothermal (a configured constant temperature) or, if
// "thermal_coupled" is set, the case's own separately-configured
// "thermal" block's converged temperature field -- both are read-only
// inputs to the EOS, not fed back into momentum/continuity (there is no
// implemented compressible pressure-correction loop to feed them into).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cfd/compressible/ThermodynamicProperties.hpp"
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
};

// Every intermediate artifact a caller might want (the GUI needs the
// case definition and residual history for display; the CLI needs the
// export summary's paths for its own printed report) -- optional,
// populated as far as the run actually got (e.g. caseDefinition is
// always set once the case parses, even if the solve itself later
// fails; simpleResult/thermalResult/exportSummary stay unset for an
// InvalidCase/ApplicationError run that never reached them).
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
  std::optional<cfd::pressure_velocity::SIMPLEResult> simpleResult;
  std::optional<cfd::thermal::ThermalResult> thermalResult;
  // P6-PHYS-001: one entry per physics.json-declared species, in
  // declaration order -- always the full declared set once the SIMPLE
  // result is fully finite (never partially populated the way a single
  // optional field could leave ambiguous which species ran), empty
  // otherwise (mirrors thermalResult staying unset when the case has no
  // thermal block, generalized from "unset" to "empty vector" for a
  // list-shaped field).
  std::vector<SpeciesRunResult> speciesResults;
  // P6-PHYS-002/003: present iff the case configured the corresponding
  // block AND the SIMPLE result was fully finite (same "run only once
  // the flow is usable" policy species/thermal already follow) -- unset
  // otherwise, never a partially-populated struct.
  std::optional<MultiphaseRunResult> multiphaseResult;
  std::optional<CompressibleRunResult> compressibleResult;
  std::optional<cfd::io::ResultExportSummary> exportSummary;
  // Populated for InvalidCase (CaseReader/CaseBuilder's own message) and
  // ApplicationError (a result-export I/O failure) -- empty otherwise,
  // same "name the offending file/field" convention CaseReader already
  // follows (never a bare "Invalid case").
  std::string errorMessage;
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
