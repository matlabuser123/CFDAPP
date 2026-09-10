#include "cfd/thermal/ThermalSolver.hpp"

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalInterface.hpp"

namespace cfd::thermal {

using cfd::algebra::BiCGSTAB;
using cfd::algebra::SolverResult;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;

namespace {

// Same per-file local helper convention as pressure_velocity::SIMPLE.cpp/
// PISO.cpp/solver::RestartSnapshot.cpp -- Field<T> has no generic
// allFinite() member, so every consumer defines its own tiny check.
bool allFinite(const ScalarField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

bool allFinite(const SurfaceField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

ScalarField toScalarField(const Vector& v) {
  ScalarField field(v.size());
  for (Index i = 0; i < v.size(); ++i) field[i] = v[i];
  return field;
}

Vector toVector(const ScalarField& field) {
  Vector v(field.size());
  for (Index i = 0; i < field.size(); ++i) v[i] = field[i];
  return v;
}

Real maxAbsoluteDifference(const ScalarField& a, const ScalarField& b) {
  Real maxDiff = 0.0;
  for (Index i = 0; i < a.size(); ++i) {
    maxDiff = std::max(maxDiff, std::abs(a[i] - b[i]));
  }
  return maxDiff;
}

// Shared outer Picard/fixed-point loop -- see ThermalSolverSettings's own
// header comment for why this loop exists at all. `assemble` is already
// bound to whichever equation (single-material convection-diffusion, or
// CHT region-aware conduction) the caller wants; this function only owns
// the iterate-until-self-consistent machinery, identical either way.
// Preconditions (checked by the caller before this runs): settings are
// valid, a BiCGSTAB solver was already constructed, initialTemperature is
// finite and correctly sized.
ThermalResult runPicardLoop(const ThermalSolverSettings& settings, BiCGSTAB& linearSolver,
                            const ScalarField& initialTemperature,
                            const std::function<EnergyAssembly(const ScalarField&)>& assemble) {
  ThermalResult result;
  ScalarField temperature = initialTemperature;
  ThermalStatus finalStatus = ThermalStatus::MaxIterations;
  Index outerIterationsRun = 0;

  for (Index outer = 0; outer < settings.maxIterations; ++outer) {
    // Sizes and finiteness are already checked (before the loop, and via
    // the allFinite() checks below on every previous iteration's output),
    // so assembly should never actually throw here -- this mirrors
    // SIMPLE.cpp's own defensive try/catch around its (similarly
    // pre-checked) momentum assembly step.
    std::optional<EnergyAssembly> assembly;
    try {
      assembly = assemble(temperature);
    } catch (const NumericalError&) {
      // assembly left empty -- fall through to the check below.
    }
    if (!assembly.has_value()) {
      finalStatus = ThermalStatus::NonFiniteState;
      outerIterationsRun = outer + 1;
      break;
    }

    const SolverResult linearResult = linearSolver.solve(assembly->system, toVector(temperature));
    result.linearIterations = linearResult.iterations;
    result.initialResidual = linearResult.initialResidual;
    result.finalResidual = linearResult.finalResidual;
    result.residualHistory = linearResult.residualHistory;

    if (!linearResult.converged()) {
      finalStatus = ThermalStatus::LinearSolveFailure;
      outerIterationsRun = outer + 1;
      break;
    }

    ScalarField newTemperature = toScalarField(linearResult.solution);
    if (!allFinite(newTemperature)) {
      finalStatus = ThermalStatus::NonFiniteState;
      outerIterationsRun = outer + 1;
      break;
    }

    const Real maxChange = maxAbsoluteDifference(newTemperature, temperature);
    result.outerChangeHistory.push_back(maxChange);
    result.maxTemperatureChange = maxChange;
    temperature = std::move(newTemperature);
    outerIterationsRun = outer + 1;

    if (maxChange < settings.tolerance) {
      finalStatus = ThermalStatus::Converged;
      break;
    }
  }

  result.temperature = std::move(temperature);
  result.iterations = outerIterationsRun;
  result.status = finalStatus;
  return result;
}

}  // namespace

ThermalSolver::ThermalSolver(ThermalSolverSettings settings) : settings_(std::move(settings)) {}

const ThermalSolverSettings& ThermalSolver::settings() const noexcept { return settings_; }

ThermalResult ThermalSolver::solve(const Mesh& mesh, const ScalarField& initialTemperature,
                                   const SurfaceField& massFlux, const ThermalProperties& thermal,
                                   const BoundaryConditionSet& temperatureBoundaries,
                                   Real volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  // Configuration problems are a legitimate solve()-time outcome a caller
  // should branch on via `status`, not an exception escaping solve() --
  // same convention as pressure_velocity::SIMPLE::solve. This includes
  // constructing the BiCGSTAB solver: LinearSolver's own constructor
  // validates its settings (e.g. maxIterations >= 1) and throws
  // InvalidArgumentError, which must be caught here rather than left to
  // propagate out of solve().
  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError("ThermalSolver::solve: maxIterations must be >= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError("ThermalSolver::solve: tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: initialTemperature size does not match mesh cell count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: massFlux size does not match mesh face count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature) || !allFinite(massFlux)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  const auto assemble = [&](const ScalarField& temperature) {
    return assembleEnergyEquation(mesh, temperature, massFlux, thermal, temperatureBoundaries,
                                  volumetricHeatSource);
  };
  return runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);
}

ThermalResult ThermalSolver::solve(const Mesh& mesh, const ScalarField& initialTemperature,
                                   const SurfaceField& massFlux,
                                   const cfd::physics::TemperatureProperty& conductivityModel,
                                   const cfd::physics::TemperatureProperty& specificHeatModel,
                                   const BoundaryConditionSet& temperatureBoundaries,
                                   Real volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError("ThermalSolver::solve: maxIterations must be >= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError("ThermalSolver::solve: tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: initialTemperature size does not match mesh cell count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "ThermalSolver::solve: massFlux size does not match mesh face count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature) || !allFinite(massFlux)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  // Re-evaluated fresh every outer iteration from the *current* temperature
  // iterate -- see this overload's own header comment on why this reuses
  // runPicardLoop's existing assemble-lambda mechanism rather than adding a
  // second loop. runPicardLoop only ever catches NumericalError out of
  // `assemble` (turning it into ThermalStatus::NonFiniteState), but
  // evaluatePropertyField signals a rejected (non-finite/non-positive)
  // property value via InvalidArgumentError -- translated to NumericalError
  // right here so a mid-iteration property excursion is reported through
  // the same status channel as any other non-finite assembled state,
  // instead of escaping solve() as an uncaught exception (this overload's
  // own "never throws for a legitimate numerical-failure outcome"
  // convention, same as solve() above).
  const auto assemble = [&](const ScalarField& temperature) -> EnergyAssembly {
    ScalarField conductivity;
    ScalarField specificHeat;
    try {
      conductivity = cfd::physics::evaluatePropertyField(mesh, temperature, conductivityModel);
      specificHeat = cfd::physics::evaluatePropertyField(mesh, temperature, specificHeatModel);
    } catch (const InvalidArgumentError& e) {
      throw NumericalError(std::string("ThermalSolver::solve (variable properties): ") + e.what());
    }
    return assembleEnergyEquation(mesh, temperature, massFlux, conductivity, specificHeat,
                                  temperatureBoundaries, volumetricHeatSource);
  };
  return runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);
}

ThermalResult ThermalSolver::solveConjugateConduction(
    const Mesh& mesh, const ScalarField& initialTemperature, const ThermalRegionMap& regions,
    const BoundaryConditionSet& temperatureBoundaries, Real volumetricHeatSource) const {
  ThermalResult result;
  result.temperature = initialTemperature;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: maxIterations must be "
          ">= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: tolerance must be finite and > 0");
    }
    if (initialTemperature.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: initialTemperature size does not match mesh "
          "cell count");
    }
    if (regions.numberOfCells() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "ThermalSolver::solveConjugateConduction: regions size does not match mesh cell count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = ThermalStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialTemperature)) {
    result.status = ThermalStatus::NonFiniteState;
    return result;
  }

  const auto assemble = [&](const ScalarField& temperature) {
    return assembleConjugateConductionEquation(mesh, temperature, regions, temperatureBoundaries,
                                               volumetricHeatSource);
  };
  return runPicardLoop(settings_, *linearSolver, initialTemperature, assemble);
}

}  // namespace cfd::thermal
