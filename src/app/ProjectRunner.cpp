#include "cfd/app/ProjectRunner.hpp"

#include <cmath>
#include <optional>

#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"

namespace cfd::app {

using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;

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
    if (!std::isfinite(result.velocity[i].x) || !std::isfinite(result.velocity[i].y)) return true;
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

ProjectRunStatus statusFor(SIMPLEStatus status) {
  switch (status) {
    case SIMPLEStatus::Converged:
      return ProjectRunStatus::Converged;
    case SIMPLEStatus::MaxIterations:
      return ProjectRunStatus::DidNotConverge;
    case SIMPLEStatus::Cancelled:
      return ProjectRunStatus::Cancelled;
    case SIMPLEStatus::MomentumFailure:
    case SIMPLEStatus::PressureCorrectionFailure:
    case SIMPLEStatus::NonFiniteState:
    case SIMPLEStatus::InvalidConfiguration:
      return ProjectRunStatus::NumericalFailure;
  }
  return ProjectRunStatus::NumericalFailure;
}

}  // namespace

ProjectRunResult ProjectRunner::run(const std::filesystem::path& caseDirectory,
                                    const ProjectRunOptions& options) {
  ProjectRunResult out;

  std::optional<cfd::io::CaseDefinition> caseDefinition;
  std::optional<cfd::io::SimulationSetup> setupOpt;
  try {
    caseDefinition = cfd::io::CaseReader{}.read(caseDirectory);
    setupOpt = cfd::io::CaseBuilder{}.build(*caseDefinition);
  } catch (const cfd::Error& e) {
    out.status = ProjectRunStatus::InvalidCase;
    out.errorMessage = e.what();
    return out;
  }
  out.caseDefinition = caseDefinition;
  const cfd::io::SimulationSetup& setup = *setupOpt;
  out.mesh = setup.mesh;

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

  const cfd::pressure_velocity::SIMPLE simple(setup.solverSettings, /*referenceCell=*/0,
                                              activeTurbulenceModel, /*temperature=*/nullptr,
                                              /*buoyancy=*/nullptr, options.progressCallback,
                                              options.cancellationCheck);
  const SIMPLEResult result =
      simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                   setup.initialVelocity, setup.initialPressure);
  out.simpleResult = result;
  out.status = statusFor(result.status);

  // Same one-way, best-available thermal solve as the pre-P5 CLI (see
  // apps/cli/main.cpp's own comment on this policy -- unchanged here).
  std::optional<cfd::thermal::ThermalResult> thermalResult;
  std::optional<cfd::io::ThermalRunMetadata> thermalMetadata;
  if (setup.thermal.has_value() && !anyNonFinite(result)) {
    const cfd::fields::SurfaceField massFlux = cfd::physics::calculateMassFlux(
        setup.mesh, result.velocity, setup.fluid, setup.velocityBoundaries);
    const cfd::thermal::ThermalSolver thermalSolver{};
    thermalResult = thermalSolver.solve(setup.mesh, *setup.initialTemperature, massFlux,
                                        *setup.thermal, *setup.temperatureBoundaries);
    thermalMetadata = cfd::io::ThermalRunMetadata{setup.thermal->conductivity(),
                                                  setup.thermal->specificHeat(),
                                                  thermalStatusName(thermalResult->status),
                                                  thermalResult->converged(),
                                                  thermalResult->iterations,
                                                  thermalResult->finalResidual};
  } else if (setup.thermal.has_value()) {
    thermalMetadata = cfd::io::ThermalRunMetadata{setup.thermal->conductivity(),
                                                  setup.thermal->specificHeat(), "NotRun",
                                                  /*converged=*/false, /*iterations=*/0,
                                                  /*finalResidual=*/0.0};
  }
  out.thermalResult = thermalResult;

  const cfd::io::RunMetadata exportMetadata{caseDefinition->caseConfig.name,
                                            caseDefinition->physics.density,
                                            caseDefinition->physics.dynamicViscosity,
                                            caseDefinition->solver.type};
  try {
    const std::optional<cfd::fields::ScalarField> temperatureForExport =
        thermalResult.has_value() ? std::optional(thermalResult->temperature) : std::nullopt;
    out.exportSummary =
        cfd::io::ResultExporter::write(caseDirectory / "results", setup.mesh, result,
                                       exportMetadata, temperatureForExport, thermalMetadata);
  } catch (const cfd::Error& e) {
    out.status = ProjectRunStatus::ApplicationError;
    out.errorMessage = e.what();
  }
  return out;
}

}  // namespace cfd::app
