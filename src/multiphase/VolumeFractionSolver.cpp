#include "cfd/multiphase/VolumeFractionSolver.hpp"

#include <cmath>
#include <optional>
#include <utility>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/multiphase/VolumeFractionEquation.hpp"

namespace cfd::multiphase {

using cfd::algebra::BiCGSTAB;
using cfd::algebra::SolverResult;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;

namespace {

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

}  // namespace

VolumeFractionSolver::VolumeFractionSolver(VolumeFractionSolverSettings settings)
    : settings_(std::move(settings)) {}

const VolumeFractionSolverSettings& VolumeFractionSolver::settings() const noexcept {
  return settings_;
}

VolumeFractionStepResult VolumeFractionSolver::step(const Mesh& mesh, const ScalarField& alphaOld,
                                                    const SurfaceField& massFlux,
                                                    const BoundaryConditionSet& alphaBoundaries,
                                                    Real dt) const {
  VolumeFractionStepResult result;
  result.alpha = alphaOld;

  bool configurationValid = true;
  std::optional<BiCGSTAB> linearSolver;
  try {
    if (alphaOld.size() != mesh.numberOfCells()) {
      throw InvalidArgumentError(
          "VolumeFractionSolver::step: alphaOld size does not match mesh cell count");
    }
    if (massFlux.size() != mesh.numberOfFaces()) {
      throw InvalidArgumentError(
          "VolumeFractionSolver::step: massFlux size does not match mesh face count");
    }
    if (!std::isfinite(dt) || !(dt > 0.0)) {
      throw InvalidArgumentError("VolumeFractionSolver::step: dt must be finite and > 0");
    }
    linearSolver.emplace(settings_.linearSolver);
  } catch (const InvalidArgumentError&) {
    configurationValid = false;
  }
  if (!configurationValid) {
    result.status = VolumeFractionStatus::InvalidConfiguration;
    return result;
  }

  if (!allFinite(alphaOld) || !allFinite(massFlux)) {
    result.status = VolumeFractionStatus::NonFiniteState;
    return result;
  }

  std::optional<VolumeFractionAssembly> assembly;
  try {
    assembly =
        assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, alphaBoundaries, dt);
  } catch (const NumericalError&) {
    // assembly left empty -- fall through to the check below.
  }
  if (!assembly.has_value()) {
    result.status = VolumeFractionStatus::NonFiniteState;
    return result;
  }

  const SolverResult linearResult = linearSolver->solve(assembly->system, toVector(alphaOld));
  result.linearIterations = linearResult.iterations;
  result.initialResidual = linearResult.initialResidual;
  result.finalResidual = linearResult.finalResidual;
  result.residualHistory = linearResult.residualHistory;

  if (!linearResult.converged()) {
    result.status = VolumeFractionStatus::LinearSolveFailure;
    return result;
  }

  ScalarField newAlpha = toScalarField(linearResult.solution);
  if (!allFinite(newAlpha)) {
    result.status = VolumeFractionStatus::NonFiniteState;
    return result;
  }

  result.alpha = std::move(newAlpha);
  result.status = VolumeFractionStatus::Converged;
  return result;
}

}  // namespace cfd::multiphase
