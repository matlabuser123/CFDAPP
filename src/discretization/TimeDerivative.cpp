#include "cfd/discretization/TimeDerivative.hpp"

#include <cmath>

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

}  // namespace cfd::discretization
