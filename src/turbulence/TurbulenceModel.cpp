#include "cfd/turbulence/TurbulenceModel.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::turbulence {

using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

ScalarField TurbulenceModel::effectiveViscosity(Real molecularViscosity) const {
  if (!std::isfinite(molecularViscosity) || !(molecularViscosity > 0.0)) {
    throw InvalidArgumentError(
        "TurbulenceModel::effectiveViscosity: molecularViscosity must be finite and > 0");
  }
  const ScalarField& turbulent = turbulentViscosity();
  ScalarField effective(turbulent.size());
  for (Index i = 0; i < turbulent.size(); ++i) {
    effective[i] = molecularViscosity + turbulent[i];
  }
  return effective;
}

void validateTurbulentViscosityField(const Mesh& mesh, const ScalarField& turbulentViscosity) {
  if (turbulentViscosity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "validateTurbulentViscosityField: size does not match mesh cell count");
  }
  for (Index i = 0; i < turbulentViscosity.size(); ++i) {
    const Real value = turbulentViscosity[i];
    if (!std::isfinite(value)) {
      throw InvalidArgumentError("validateTurbulentViscosityField: non-finite value at cell " +
                                 std::to_string(i));
    }
    if (value < 0.0) {
      throw InvalidArgumentError("validateTurbulentViscosityField: negative value at cell " +
                                 std::to_string(i));
    }
  }
}

}  // namespace cfd::turbulence
