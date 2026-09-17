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

}  // namespace cfd::discretization
