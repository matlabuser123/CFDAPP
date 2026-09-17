#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cfd/app/ProjectRunner.hpp"
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
      << "  cfdapp --case <case-directory>\n\n"
      << "Options:\n"
      << "  --case <path>   Run a CFD case\n"
      << "  --help          Show help\n"
      << "  --version       Show version\n";
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

int runCase(const std::string& caseDirectory) {
  const ProjectRunResult run = cfd::app::ProjectRunner::run(caseDirectory);

  if (run.status == ProjectRunStatus::InvalidCase) {
    std::cerr << "Case configuration error: " << run.errorMessage << "\n";
    return cfd::app::exitCodeFor(run.status);
  }
  if (run.status == ProjectRunStatus::ApplicationError && !run.exportSummary.has_value()) {
    std::cerr << "Result export error: " << run.errorMessage << "\n";
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
      if (i + 1 >= args.size()) {
        std::cerr << "Error: --case requires a directory argument\n\n";
        printUsage(std::cerr);
        return cfd::app::exitCodeFor(ProjectRunStatus::ApplicationError);
      }
      caseDirectory = args[++i];
      haveCase = true;
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

  return runCase(caseDirectory);
}
