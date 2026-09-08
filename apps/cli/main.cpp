#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Version.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/ResultExporter.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

// Exit codes (TODO.md P1 -- Case System section 32). Documented here
// rather than left implicit in a handful of `return N` statements, and
// tested (tests/integration/case/test_case_cli.cpp) so they don't drift.
namespace {

enum ExitCode : int {
  kExitConverged = 0,
  kExitApplicationError = 1,  // bad CLI usage, or a result-export I/O failure (e.g. an
                              // unwritable results/ directory) -- neither the case nor the
                              // solver run itself was at fault.
  kExitInvalidCase = 2,       // CaseReader/CaseBuilder rejected the case.
  kExitDidNotConverge = 3,    // SIMPLEStatus::MaxIterations.
  kExitNumericalFailure = 4,  // Momentum/PressureCorrection failure, non-finite state, invalid
                              // solver configuration -- SIMPLE itself, not the case files.
};

void printUsage(std::ostream& out) {
  out << cfd::core::projectName() << "\n\n"
      << "Usage:\n"
      << "  cfdapp --case <case-directory>\n\n"
      << "Options:\n"
      << "  --case <path>   Run a CFD case\n"
      << "  --help          Show help\n"
      << "  --version       Show version\n";
}

// SIMPLEStatus -> a short, stable label for the "Converged:"/status line
// -- kept here rather than relying on the raw enum's integer value
// reaching the user (TODO.md P1 section 31's "print result summary").
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
  }
  return "no (unknown status)";
}

int exitCodeFor(cfd::pressure_velocity::SIMPLEStatus status) {
  using cfd::pressure_velocity::SIMPLEStatus;
  if (status == SIMPLEStatus::Converged) return kExitConverged;
  if (status == SIMPLEStatus::MaxIterations) return kExitDidNotConverge;
  return kExitNumericalFailure;
}

bool anyNonFinite(const cfd::pressure_velocity::SIMPLEResult& result) {
  for (cfd::Index i = 0; i < result.velocity.size(); ++i) {
    if (!std::isfinite(result.velocity[i].x) || !std::isfinite(result.velocity[i].y)) return true;
  }
  for (cfd::Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) return true;
  }
  return false;
}

// TODO.md P1 section 31/46: concise, and machine-readable enough for
// future automation (fixed "Label: value" lines rather than a
// hand-formatted table), plus the paths of whatever ResultExporter
// actually wrote (section 46's example) -- printed only after a
// successful export, since runCase does not call this until then.
void printReport(std::ostream& out, const cfd::io::CaseDefinition& caseDefinition,
                 const cfd::pressure_velocity::SIMPLEResult& result,
                 const cfd::io::ResultExportSummary& exported) {
  out << cfd::core::projectName() << "\n\n"
      << "Case: " << caseDefinition.caseConfig.name << "\n"
      << "Mesh: " << caseDefinition.mesh.nx << " x " << caseDefinition.mesh.ny << "\n"
      << "Solver: " << caseDefinition.solver.type << "\n\n"
      << "Converged: " << statusLabel(result.status) << "\n"
      << "Iterations: " << result.iterations << "\n\n"
      << "U residual: " << result.finalUResidual << "\n"
      << "V residual: " << result.finalVResidual << "\n"
      << "P residual: " << result.finalPressureResidual << "\n"
      << "Continuity: " << result.finalContinuityResidual << "\n"
      << "Mass imbalance: " << result.globalMassImbalance << "\n"
      << "NaN/Inf: " << (anyNonFinite(result) ? "yes" : "no") << "\n\n"
      << "Results:\n"
      << "  JSON:      " << exported.metadataPath.string() << "\n"
      << "  Residuals: " << exported.residualsCsvPath.string() << "\n";
  if (exported.fieldsCsvPath.has_value()) {
    out << "  CSV:       " << exported.fieldsCsvPath->string() << "\n";
  }
  if (exported.vtkPath.has_value()) {
    out << "  VTK:       " << exported.vtkPath->string() << "\n";
  }
}

int runCase(const std::string& caseDirectory) {
  // SimulationSetup holds a Mesh (no default constructor), so it cannot
  // be default-constructed ahead of the try block the way a plain-value
  // CaseDefinition can -- everything that needs `setup` (including
  // SIMPLE::solve(), which never throws for an ordinary solver failure;
  // those are reported through SIMPLEResult::status instead, not an
  // exception) lives inside this one try block instead.
  try {
    const cfd::io::CaseDefinition caseDefinition = cfd::io::CaseReader{}.read(caseDirectory);
    const cfd::io::SimulationSetup setup = cfd::io::CaseBuilder{}.build(caseDefinition);

    // Reference cell stays the deterministic default 0 (TODO.md P1
    // section 43) -- not exposed through solver.json.
    const cfd::pressure_velocity::SIMPLE simple(setup.solverSettings, /*referenceCell=*/0);
    const cfd::pressure_velocity::SIMPLEResult result =
        simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                     setup.initialVelocity, setup.initialPressure);

    // Export happens for every status, including a failed solve (P1 --
    // Result Export section 29): metadata.json/residuals.csv always,
    // fields.csv/solution.vtk only if the fields are finite. Results live
    // under the case directory (section 2), never before the solver
    // result exists (section 28).
    const cfd::io::RunMetadata exportMetadata{
        caseDefinition.caseConfig.name, caseDefinition.physics.density,
        caseDefinition.physics.dynamicViscosity, caseDefinition.solver.type};
    cfd::io::ResultExportSummary exported;
    try {
      exported = cfd::io::ResultExporter::write(std::filesystem::path(caseDirectory) / "results",
                                                setup.mesh, result, exportMetadata);
    } catch (const cfd::Error& e) {
      // An I/O failure writing results (e.g. an unwritable directory) is
      // neither an invalid case nor a solver failure -- report and exit
      // distinctly from both (exit code 1, see the ExitCode comment
      // above) rather than folding it into "Case configuration error".
      std::cerr << "Result export error: " << e.what() << "\n";
      return kExitApplicationError;
    }

    printReport(std::cout, caseDefinition, result, exported);
    return exitCodeFor(result.status);
  } catch (const cfd::Error& e) {
    // IOError (missing/unreadable/malformed file), CaseConfigurationError
    // (invalid content), or InvalidArgumentError (a runtime object
    // CaseBuilder constructed, e.g. FluidProperties, rejected an
    // already-parsed value on its own construction-time validation) --
    // all three mean the *case* is invalid, so they map to the same exit
    // code (section 32).
    std::cerr << "Case configuration error: " << e.what() << "\n";
    return kExitInvalidCase;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);

  if (args.empty()) {
    printUsage(std::cerr);
    return kExitApplicationError;
  }

  std::string caseDirectory;
  bool haveCase = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--help" || arg == "-h") {
      printUsage(std::cout);
      return kExitConverged;
    }
    if (arg == "--version") {
      std::cout << cfd::core::projectName() << " " << cfd::core::versionString() << "\n";
      return kExitConverged;
    }
    if (arg == "--case") {
      if (i + 1 >= args.size()) {
        std::cerr << "Error: --case requires a directory argument\n\n";
        printUsage(std::cerr);
        return kExitApplicationError;
      }
      caseDirectory = args[++i];
      haveCase = true;
      continue;
    }
    std::cerr << "Error: unrecognized argument \"" << arg << "\"\n\n";
    printUsage(std::cerr);
    return kExitApplicationError;
  }

  if (!haveCase) {
    std::cerr << "Error: --case <case-directory> is required\n\n";
    printUsage(std::cerr);
    return kExitApplicationError;
  }

  return runCase(caseDirectory);
}
