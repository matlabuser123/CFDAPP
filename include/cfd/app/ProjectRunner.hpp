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
// Deliberately still steady-incompressible-SIMPLE(+thermal) scope only,
// exactly matching what CaseReader/CaseBuilder support today -- species/
// multiphase/compressible case-config parsing is a disclosed, separate,
// not-yet-done prerequisite (see TODO.md's own P3-PHYS-006/P4 status
// notes), not silently expanded here.

#include <filesystem>
#include <optional>
#include <string>

#include "cfd/io/case/CaseDefinition.hpp"
#include "cfd/io/ResultExporter.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEProgress.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"
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
