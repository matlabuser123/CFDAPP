#include "cfd/species/SpeciesSolver.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/species/SpeciesEquation.hpp"

namespace cfd::species {

using cfd::algebra::BiCGSTAB;
using cfd::algebra::SolverResult;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::physics::FluidProperties;

namespace {

// Same per-file local helper convention as ThermalSolver.cpp/SIMPLE.cpp.
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

// Outer Picard/fixed-point loop -- structurally identical to
// ThermalSolver.cpp's own private runPicardLoop (see this file's own
// header comment for why it exists), duplicated here rather than shared
// across namespaces: it is a small, self-contained helper bound to one
// equation-specific `assemble` lambda, and this codebase has no existing
// precedent for factoring such a helper out across physics domains (this
// task's own section 2: "Do not create a separate numerical framework for
// species" cuts the other way too -- a premature shared abstraction is
// its own kind of new framework).
SpeciesResult runPicardLoop(const SpeciesSolverSettings& settings, BiCGSTAB& linearSolver,
                            const ScalarField& initialConcentration,
                            const std::function<SpeciesAssembly(const ScalarField&)>& assemble) {
  SpeciesResult result;
  ScalarField concentration = initialConcentration;
  SpeciesStatus finalStatus = SpeciesStatus::MaxIterations;
  Index outerIterationsRun = 0;

  for (Index outer = 0; outer < settings.maxIterations; ++outer) {
    std::optional<SpeciesAssembly> assembly;
    try {
      assembly = assemble(concentration);
    } catch (const NumericalError&) {
      // assembly left empty -- fall through to the check below.
    }
    if (!assembly.has_value()) {
      finalStatus = SpeciesStatus::NonFiniteState;
      outerIterationsRun = outer + 1;
      break;
    }

    const SolverResult linearResult = linearSolver.solve(assembly->system, toVector(concentration));
    result.linearIterations = linearResult.iterations;
    result.initialResidual = linearResult.initialResidual;
    result.finalResidual = linearResult.finalResidual;
    result.residualHistory = linearResult.residualHistory;

    if (!linearResult.converged()) {
      finalStatus = SpeciesStatus::LinearSolveFailure;
      outerIterationsRun = outer + 1;
      break;
    }

    ScalarField newConcentration = toScalarField(linearResult.solution);
    if (!allFinite(newConcentration)) {
      finalStatus = SpeciesStatus::NonFiniteState;
      outerIterationsRun = outer + 1;
      break;
    }

    const Real maxChange = maxAbsoluteDifference(newConcentration, concentration);
    result.outerChangeHistory.push_back(maxChange);
    result.maxConcentrationChange = maxChange;
    concentration = std::move(newConcentration);
    outerIterationsRun = outer + 1;

    if (maxChange < settings.tolerance) {
      finalStatus = SpeciesStatus::Converged;
      break;
    }
  }

  result.concentration = std::move(concentration);
  result.iterations = outerIterationsRun;
  result.status = finalStatus;
  return result;
}

}  // namespace

SpeciesSolver::SpeciesSolver(SpeciesSolverSettings settings) : settings_(std::move(settings)) {}

const SpeciesSolverSettings& SpeciesSolver::settings() const noexcept { return settings_; }

SpeciesResult SpeciesSolver::solve(const Mesh& mesh, const ScalarField& initialConcentration,
                                   const SurfaceField& massFlux, const FluidProperties& fluid,
                                   const SpeciesProperties& species,
                                   const BoundaryConditionSet& concentrationBoundaries,
                                   Real volumetricSource) const {
  SpeciesResult result;
  result.concentration = initialConcentration;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (settings_.maxIterations == 0) {
      throw InvalidArgumentError("SpeciesSolver::solve: maxIterations must be >= 1");
    }
    if (!std::isfinite(settings_.tolerance) || !(settings_.tolerance > 0.0)) {
      throw InvalidArgumentError("SpeciesSolver::solve: tolerance must be finite and > 0");
    }
    if (initialConcentration.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "SpeciesSolver::solve: initialConcentration size does not match mesh cell count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "SpeciesSolver::solve: massFlux size does not match mesh face count");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = SpeciesStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(initialConcentration) || !allFinite(massFlux)) {
    result.status = SpeciesStatus::NonFiniteState;
    return result;
  }

  const auto assemble = [&](const ScalarField& concentration) {
    return assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species,
                                            concentrationBoundaries, volumetricSource);
  };
  return runPicardLoop(settings_, *linearSolver, initialConcentration, assemble);
}

}  // namespace cfd::species
