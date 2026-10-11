#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/core/Logger.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Version.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

// P5 -- Application, section 0: the CLI is now a thin argument-parsing/
// printing wrapper around cfd::app::ProjectRunner -- the one production
// solver backend also used by apps/gui's SimulationController (see
// ProjectRunner.hpp's own header comment). Every exit code below is
// exactly cfd::app::exitCodeFor(result.status); this file no longer
// computes its own.

namespace {

using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;

void printUsage(std::ostream& out) {
  out << cfd::core::projectName() << "\n\n"
      << "Usage:\n"
      << "  cfdapp --case <case-directory>\n"
      << "  cfdapp --case <dir> --checkpoint <file> [--checkpoint-every <n>]\n"
      << "  cfdapp --case <dir> --resume <file>\n\n"
      << "Options:\n"
      << "  --case <path>            Run a CFD case\n"
      << "  --checkpoint <file>      Write a solver checkpoint to <file>\n"
      << "  --checkpoint-every <n>   Also write it every n outer iterations\n"
      << "                           (requires --checkpoint; n >= 1)\n"
      << "  --resume <file>          Continue a solve from a checkpoint written\n"
      << "                           earlier for this same case and mesh\n"
      << "  --log-level <level>      Diagnostics verbosity on stderr: " << cfd::logLevelNames()
      << "\n"
      << "                           (default info; results always go to stdout)\n"
      << "  --help                   Show help\n"
      << "  --version                Show version\n\n"
      << "A checkpoint stores solver state -- velocity, pressure, the corrected face\n"
      << "mass flux, the completed iteration count and the residual history -- so a\n"
      << "resumed run continues the same solve instead of starting a new one.\n"
      << "Resuming needs the same mesh and the same case configuration; only\n"
      << "solver.json max_iterations may differ, so a run can be given more\n"
      << "iterations and finished.\n";
}

std::string_view statusLabel(cfd::pressure_velocity::SIMPLEStatus status) {
  using cfd::pressure_velocity::SIMPLEStatus;
  switch (status) {
    case SIMPLEStatus::Converged:
      return "yes";
    case SIMPLEStatus::MaxIterations:
      return "no (max iterations reached)";
    case SIMPLEStatus::MomentumFailure:
      return "no (momentum solve failed)";
    case SIMPLEStatus::PressureCorrectionFailure:
      return "no (pressure correction solve failed)";
    case SIMPLEStatus::NonFiniteState:
      return "no (non-finite state)";
    case SIMPLEStatus::InvalidConfiguration:
      return "no (invalid solver configuration)";
    case SIMPLEStatus::Cancelled:
      return "no (cancelled)";
    case SIMPLEStatus::Stagnated:
      return "no (stagnated)";
    case SIMPLEStatus::Diverging:
      return "no (diverging)";
  }
  return "no (unknown status)";
}

std::string_view thermalStatusLabel(cfd::thermal::ThermalStatus status) {
  using cfd::thermal::ThermalStatus;
  switch (status) {
    case ThermalStatus::Converged:
      return "yes";
    case ThermalStatus::MaxIterations:
      return "no (max iterations reached)";
    case ThermalStatus::LinearSolveFailure:
      return "no (linear solve failed)";
    case ThermalStatus::NonFiniteState:
      return "no (non-finite state)";
    case ThermalStatus::InvalidConfiguration:
      return "no (invalid solver configuration)";
  }
  return "no (unknown status)";
}

// P6-PHYS-001: the species counterpart of thermalStatusLabel above.
std::string_view speciesStatusLabel(cfd::species::SpeciesStatus status) {
  using cfd::species::SpeciesStatus;
  switch (status) {
    case SpeciesStatus::Converged:
      return "yes";
    case SpeciesStatus::MaxIterations:
      return "no (max iterations reached)";
    case SpeciesStatus::LinearSolveFailure:
      return "no (linear solve failed)";
    case SpeciesStatus::NonFiniteState:
      return "no (non-finite state)";
    case SpeciesStatus::InvalidConfiguration:
      return "no (invalid solver configuration)";
  }
  return "no (unknown status)";
}

bool anyNonFinite(const cfd::pressure_velocity::SIMPLEResult& result) {
  for (cfd::Index i = 0; i < result.velocity.size(); ++i) {
    if (!cfd::isFinite(result.velocity[i])) return true;
  }
  for (cfd::Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) return true;
  }
  return false;
}

// "nx x ny", "nx x ny x nz (3D)" (P12-MESH-006), or for a P12-MESH-003
// multiblock mesh its block and cell counts.
std::string meshLabel(const cfd::io::MeshConfig& mesh) {
  if (mesh.nz > 0) {
    return std::to_string(mesh.nx) + " x " + std::to_string(mesh.ny) + " x " +
           std::to_string(mesh.nz) + " (3D)";
  }
  if (mesh.type != "multiblock") return std::to_string(mesh.nx) + " x " + std::to_string(mesh.ny);
  cfd::Index cells = 0;
  for (const auto& block : mesh.blocks) cells += block.nx * block.ny;
  return "multiblock, " + std::to_string(mesh.blocks.size()) + " blocks, " + std::to_string(cells) +
         " cells";
}

// P12-MESH-004: the production mesh-quality summary -- status, the key
// metrics with their worst entity, and every warning / informational issue
// (fatal issues never get here: an Invalid mesh is a case error).
void printMeshQuality(std::ostream& out, const cfd::mesh::MeshQualityReport& q) {
  const auto worst = [](const cfd::mesh::MeshQualityMetric& m, const char* entity) {
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%.6g (%s %zu at (%.6g, %.6g))", m.maximum, entity,
                  static_cast<std::size_t>(m.worstId), m.worstLocation.x, m.worstLocation.y);
    return m.count > 0 ? std::string(buffer) : std::string("n/a");
  };
  char areas[96];
  std::snprintf(areas, sizeof(areas), "%.6g .. %.6g", q.cellArea.minimum, q.cellArea.maximum);
  out << "Mesh quality: " << cfd::mesh::meshQualityStatusName(q.status) << "\n"
      << "  cells: " << q.cellCount << ", cell area: " << areas
      << ", degenerate cells: " << q.degenerateCells << ", invalid faces: " << q.invalidFaces
      << "\n"
      << "  max aspect ratio: " << worst(q.aspectRatio, "cell") << "\n"
      << "  max non-orthogonality (deg): " << worst(q.nonOrthogonality, "face") << "\n"
      << "  max skewness: " << worst(q.skewness, "face") << "\n"
      << "  max expansion ratio: " << worst(q.expansionRatio, "face") << "\n";
  for (const auto& issue : q.issues) {
    out << "  " << cfd::mesh::formatMeshQualityIssue(issue) << "\n";
  }
}

void printReport(std::ostream& out, const ProjectRunResult& run) {
  out << cfd::core::projectName() << "\n\n"
      << "Case: " << run.caseDefinition->caseConfig.name << "\n"
      << "Mesh: " << meshLabel(run.caseDefinition->mesh) << "\n";
  if (run.meshQuality.has_value()) printMeshQuality(out, *run.meshQuality);
  out << "Solver: " << run.caseDefinition->solver.type << "\n\n"
      << "Converged: " << statusLabel(run.simpleResult->status) << "\n"
      << "Iterations: " << run.simpleResult->iterations << "\n\n"
      << "U residual: " << run.simpleResult->finalUResidual << "\n"
      << "V residual: " << run.simpleResult->finalVResidual << "\n";
  // P12-MESH-006: a 3D run adds its W residual (a 2D report is unchanged).
  const bool threeDimensional = run.mesh.has_value() && run.mesh->dimension() == 3;
  if (threeDimensional) out << "W residual: " << run.simpleResult->finalWResidual << "\n";
  out << "P residual: " << run.simpleResult->finalPressureResidual << "\n"
      << "Continuity: " << run.simpleResult->finalContinuityResidual << "\n"
      << "Mass imbalance: " << run.simpleResult->globalMassImbalance << "\n"
      << "NaN/Inf: " << (anyNonFinite(*run.simpleResult) ? "yes" : "no") << "\n\n";
  // P12-MESH-006: the 3D mass balance of the corrected (canonical) face flux.
  if (threeDimensional && run.simpleResult->massFlux.size() == run.mesh->numberOfFaces()) {
    const auto balance = cfd::physics::computeMassBalance(*run.mesh, run.simpleResult->massFlux);
    out << "Face flux: " << cfd::pressure_velocity::faceFluxSchemeName(run.simpleResult->faceFlux)
        << "\n"
        << "Mass balance: inflow " << balance.inflow << ", outflow " << balance.outflow
        << ", relative imbalance " << balance.relativeImbalance << "\n"
        << "Cell continuity: max " << balance.maxCellImbalance << ", rms "
        << balance.rmsCellImbalance << ", normalized " << balance.normalizedContinuity << "\n\n";
  }
  // P12-NUM-004: printed only when there is something to report, so the
  // report of a default (robustness features off, no failure) run is
  // unchanged.
  const auto& robustness = run.simpleResult->robustness;
  if (!robustness.statusDetail.empty()) {
    out << "Status detail: " << robustness.statusDetail << "\n";
  }
  if (robustness.linearSolverFallbacks > 0) {
    out << "Linear-solver fallbacks: " << robustness.linearSolverFallbacks << " ("
        << robustness.linearSolverFallbackRecoveries << " recovered)\n";
  }
  if (!robustness.statusDetail.empty() || robustness.linearSolverFallbacks > 0) {
    out << "\n";
  }
  if (run.thermalResult.has_value()) {
    out << "Thermal converged: " << thermalStatusLabel(run.thermalResult->status) << "\n"
        << "Thermal iterations: " << run.thermalResult->iterations << "\n\n";
  }
  // P6-PHYS-001: one line pair per declared species, in declaration
  // order -- same shape as the thermal block above, generalized from one
  // optional field to a list.
  for (const auto& speciesRun : run.speciesResults) {
    out << "Species " << speciesRun.name
        << " converged: " << speciesStatusLabel(speciesRun.result.status) << "\n"
        << "Species " << speciesRun.name << " iterations: " << speciesRun.result.iterations
        << "\n\n";
  }
  // TD-047: the REACTING block, which this report omitted entirely. The species loop above
  // iterates `run.speciesResults` -- a field the reacting path does NOT fill, because it fills
  // `ReactingRunResult::speciesResults` instead -- so a reacting run printed no species lines
  // at all and its failures reached neither stdout nor the exit code. Reported in the same
  // shape as the thermal and species blocks above.
  if (run.reactingResult.has_value()) {
    const auto& reacting = *run.reactingResult;
    for (const auto& speciesRun : reacting.speciesResults) {
      out << "Reacting species " << speciesRun.name
          << " converged: " << speciesStatusLabel(speciesRun.result.status) << "\n"
          << "Reacting species " << speciesRun.name
          << " iterations: " << speciesRun.result.iterations << "\n";
    }
    out << "Reacting temperature converged: "
        << (reacting.temperatureSolveConverged ? "yes" : "no (linear solve failed)") << "\n"
        << "Reacting temperature iterations: " << reacting.temperatureIterations
        << "\n"
        // The aggregate is what the run STATUS is now derived from, so printing it keeps the
        // report and the exit code from being able to disagree.
        << "Reacting any linear solve failed: " << (reacting.anyLinearSolveFailed ? "yes" : "no")
        << "\n"
        << "Reacting steps taken: " << reacting.stepsTaken << "\n\n";
  }
  // P6-PHYS-002: mirrors the thermal block above -- there is exactly one
  // multiphase block per case (unlike species' own list).
  if (run.multiphaseResult.has_value()) {
    out << "Multiphase alpha converged: "
        << (run.multiphaseResult->alphaStep.converged() ? "yes" : "no") << "\n"
        << "Multiphase phase1 volume: " << run.multiphaseResult->phase1Volume << "\n\n";
  }
  // P6-PHYS-003: this foundation has no genuine converged/not-converged
  // solve (see ProjectRunner.hpp's own header comment) -- "evaluated"
  // reports whether the post-hoc pass actually ran.
  if (run.compressibleResult.has_value()) {
    out << "Compressible evaluated: yes\n"
        << "Compressible max Mach number: " << run.compressibleResult->machMax << "\n\n";
  }
  // P13-001: only printed when checkpointing was actually asked for, so the
  // report of a plain run is unchanged.
  if (run.resumedFromIteration > 0) {
    out << "Resumed from iteration: " << run.resumedFromIteration << "\n";
  }
  if (run.checkpointsWritten > 0 && run.checkpointPath.has_value()) {
    out << "Checkpoints written: " << run.checkpointsWritten << " -> "
        << run.checkpointPath->string() << "\n";
  }
  if (run.resumedFromIteration > 0 || run.checkpointsWritten > 0) out << "\n";
  if (run.exportSummary.has_value()) {
    out << "Results:\n"
        << "  JSON:      " << run.exportSummary->metadataPath.string() << "\n"
        << "  Residuals: " << run.exportSummary->residualsCsvPath.string() << "\n";
    if (run.exportSummary->fieldsCsvPath.has_value()) {
      out << "  CSV:       " << run.exportSummary->fieldsCsvPath->string() << "\n";
    }
    if (run.exportSummary->vtkPath.has_value()) {
      out << "  VTK:       " << run.exportSummary->vtkPath->string() << "\n";
    }
  }
}

int runCase(const std::string& caseDirectory, const cfd::app::ProjectRunOptions& options) {
  const ProjectRunResult run = cfd::app::ProjectRunner::run(caseDirectory, options);

  if (run.status == ProjectRunStatus::InvalidCase) {
    std::cerr << "Case configuration error: " << run.errorMessage << "\n";
    return cfd::app::exitCodeFor(run.status);
  }
  if (run.status == ProjectRunStatus::ApplicationError && !run.exportSummary.has_value()) {
    // P13-001: a checkpoint problem lands here too -- it is reported and the
    // run fails, never silently ignored. The message names the file and the
    // reason (ProjectRunner's own checkpointError text).
    std::cerr << "Run error: " << run.errorMessage << "\n";
    return cfd::app::exitCodeFor(run.status);
  }

  printReport(std::cout, run);
  return cfd::app::exitCodeFor(run.status);
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);

  if (args.empty()) {
    printUsage(std::cerr);
    return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
  }

  std::string caseDirectory;
  bool haveCase = false;
  cfd::app::ProjectRunOptions options;
  // P13-005 (V4), amendment A1. The CLI reports everything itself -- the
  // report on stdout, its own error lines on stderr -- so the diagnostic
  // trail is opt-in via --log-level and a default run's stderr is byte-for-
  // byte what it was. Warning rather than silent, so the two logging call
  // sites that existed before this item (both warning()) still appear
  // exactly as they did: criterion C3.
  cfd::Logger::instance().setLevel(cfd::LogLevel::Warning);
  // P13-001: an option that takes a value, resolved once so --case,
  // --checkpoint and --resume all report a missing argument the same way.
  const auto takeValue = [&args](std::size_t& i, const char* flag, std::string& target) {
    if (i + 1 >= args.size()) {
      std::cerr << "Error: " << flag << " requires an argument\n\n";
      return false;
    }
    target = args[++i];
    return true;
  };
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--help" || arg == "-h") {
      printUsage(std::cout);
      return cfd::app::exitCodeFor(ProjectRunStatus::Converged);
    }
    if (arg == "--version") {
      std::cout << cfd::core::projectName() << " " << cfd::core::versionString() << "\n";
      return cfd::app::exitCodeFor(ProjectRunStatus::Converged);
    }
    if (arg == "--case") {
      if (!takeValue(i, "--case", caseDirectory)) {
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      haveCase = true;
      continue;
    }
    if (arg == "--checkpoint") {
      std::string value;
      if (!takeValue(i, "--checkpoint", value)) {
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      options.checkpointPath = std::filesystem::path(value);
      continue;
    }
    if (arg == "--resume") {
      std::string value;
      if (!takeValue(i, "--resume", value)) {
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      options.resumeFrom = std::filesystem::path(value);
      continue;
    }
    if (arg == "--checkpoint-every") {
      std::string value;
      if (!takeValue(i, "--checkpoint-every", value)) {
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      // Rejected here rather than turned into a silent 0: "--checkpoint-every
      // ten" meaning "never checkpoint" is exactly the kind of quiet
      // misbehaviour a production run must not have.
      char* end = nullptr;
      const long long parsed = std::strtoll(value.c_str(), &end, 10);
      if (end == value.c_str() || *end != '\0' || parsed < 1) {
        std::cerr << "Error: --checkpoint-every requires a positive integer, got \"" << value
                  << "\"\n\n";
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      options.checkpointInterval = static_cast<cfd::Index>(parsed);
      continue;
    }
    if (arg == "--log-level") {
      std::string value;
      if (!takeValue(i, "--log-level", value)) {
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      // P13-005 (V1/V2). Rejected, not ignored: a run told to be quiet that
      // was not would be a surprise, and one told to be verbose that silently
      // was not would send a user hunting for diagnostics that were never
      // going to appear. The valid names come from the logger, so there is one
      // vocabulary rather than a second copy here that can drift.
      const std::optional<cfd::LogLevel> level = cfd::parseLogLevel(value);
      if (!level.has_value()) {
        std::cerr << "Error: --log-level must be one of " << cfd::logLevelNames() << ", got \""
                  << value << "\"\n\n";
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      cfd::Logger::instance().setLevel(*level);
      continue;
    }
    std::cerr << "Error: unrecognized argument \"" << arg << "\"\n\n";
    printUsage(std::cerr);
    return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
  }

  if (!haveCase) {
    std::cerr << "Error: --case <case-directory> is required\n\n";
    printUsage(std::cerr);
    return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
  }
  // P13-001: --checkpoint-every says how often to write the file named by
  // --checkpoint. Without a file to write there is nothing for it to mean,
  // and silently accepting it would segment the solve for no observable
  // reason, so it is an error rather than a no-op.
  if (options.checkpointInterval > 0 && !options.checkpointPath.has_value()) {
    std::cerr << "Error: --checkpoint-every requires --checkpoint <file>\n\n";
    printUsage(std::cerr);
    return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
  }

  return runCase(caseDirectory, options);
}
