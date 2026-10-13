#include "cfd/discretization/TimeDerivative.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::discretization {

using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

TimeDerivativeCoefficients implicitEulerTimeDerivative(const Mesh& mesh, const ScalarField& phiOld,
                                                       Real density, Real dt) {
  if (phiOld.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "implicitEulerTimeDerivative: phiOld size does not match mesh cell count");
  }
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("implicitEulerTimeDerivative: dt must be finite and > 0");
  }
  if (!std::isfinite(density) || !(density > 0.0)) {
    throw InvalidArgumentError("implicitEulerTimeDerivative: density must be finite and > 0");
  }

  TimeDerivativeCoefficients result{ScalarField(mesh.numberOfCells(), 0.0),
                                    ScalarField(mesh.numberOfCells(), 0.0)};
  for (const auto& cell : mesh.cells()) {
    const Real aPTime = (density * cell.volume()) / dt;
    result.diagonal[cell.id()] = aPTime;
    result.source[cell.id()] = aPTime * phiOld[cell.id()];
  }
  return result;
}

TimeDerivativeCoefficients aleImplicitEulerTimeDerivative(const Mesh& mesh,
                                                          const ScalarField& phiOld,
                                                          const ScalarField& previousVolume,
                                                          Real density, Real dt) {
  if (phiOld.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "aleImplicitEulerTimeDerivative: phiOld size does not match mesh cell count");
  }
  if (previousVolume.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "aleImplicitEulerTimeDerivative: previousVolume size does not match mesh cell count");
  }
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("aleImplicitEulerTimeDerivative: dt must be finite and > 0");
  }
  if (!std::isfinite(density) || !(density > 0.0)) {
    throw InvalidArgumentError("aleImplicitEulerTimeDerivative: density must be finite and > 0");
  }

  TimeDerivativeCoefficients result{ScalarField(mesh.numberOfCells(), 0.0),
                                    ScalarField(mesh.numberOfCells(), 0.0)};
  for (const auto& cell : mesh.cells()) {
    const Real oldVolume = previousVolume[cell.id()];
    if (!std::isfinite(oldVolume) || !(oldVolume > 0.0)) {
      throw InvalidArgumentError("aleImplicitEulerTimeDerivative: previous volume of cell " +
                                 std::to_string(cell.id()) + " must be finite and > 0");
    }
    const Real aPTime = (density * cell.volume()) / dt;
    const Real aPTimeOld = (density * oldVolume) / dt;
    result.diagonal[cell.id()] = aPTime;
    result.source[cell.id()] = aPTimeOld * phiOld[cell.id()];
  }
  return result;
}

// P14 Phase B3 -- per-cell density, conservative form. The scalar overload
// above is untouched; this is additive.
TimeDerivativeCoefficients implicitEulerTimeDerivative(const cfd::mesh::Mesh& mesh,
                                                       const ScalarField& phiOld,
                                                       const ScalarField& densityOld,
                                                       const ScalarField& densityNew, Real dt) {
  const Index n = mesh.numberOfCells();
  if (phiOld.size() != n) {
    throw InvalidArgumentError(
        "implicitEulerTimeDerivative: phiOld size does not match mesh cell count");
  }
  if (densityOld.size() != n || densityNew.size() != n) {
    throw InvalidArgumentError(
        "implicitEulerTimeDerivative: density size does not match mesh cell count");
  }
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("implicitEulerTimeDerivative: dt must be finite and > 0");
  }

  TimeDerivativeCoefficients result{ScalarField(n, 0.0), ScalarField(n, 0.0)};
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Real rhoOld = densityOld[id];
    const Real rhoNew = densityNew[id];
    if (!std::isfinite(rhoOld) || !(rhoOld > 0.0) || !std::isfinite(rhoNew) || !(rhoNew > 0.0)) {
      throw InvalidArgumentError("implicitEulerTimeDerivative: density in cell " +
                                 std::to_string(id) + " must be finite and > 0");
    }
    // Same operand order as the scalar overload -- (rho * V) / dt -- so a
    // constant density field reproduces it exactly rather than merely closely.
    result.diagonal[id] = (rhoNew * cell.volume()) / dt;
    result.source[id] = ((rhoOld * cell.volume()) / dt) * phiOld[id];
  }
  return result;
}

}  // namespace cfd::discretization
