#include "cfd/thermal/TurbulentConductivity.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::thermal {

using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

namespace {

void requireNonNegative(Real value, const char* name, const char* context) {
  if (!std::isfinite(value) || value < 0.0) {
    throw InvalidArgumentError(std::string(context) + ": " + name + " must be finite and >= 0");
  }
}

void requirePositive(Real value, const char* name, const char* context) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string(context) + ": " + name + " must be finite and > 0");
  }
}

}  // namespace

Real turbulentConductivity(Real turbulentViscosity, Real specificHeat, Real turbulentPrandtl) {
  constexpr const char* kContext = "turbulentConductivity";
  requireNonNegative(turbulentViscosity, "turbulentViscosity", kContext);
  requirePositive(specificHeat, "specificHeat", kContext);
  requirePositive(turbulentPrandtl, "turbulentPrandtl", kContext);
  // mu_t = 0 gives exactly 0.0 here: 0.0 * cp is 0.0, and 0.0 / Pr_t is 0.0,
  // for every finite positive cp and Pr_t. The laminar limit is therefore an
  // identity rather than a rounding.
  return turbulentViscosity * specificHeat / turbulentPrandtl;
}

Real turbulentThermalDiffusivity(Real turbulentViscosity, Real density, Real turbulentPrandtl) {
  constexpr const char* kContext = "turbulentThermalDiffusivity";
  requireNonNegative(turbulentViscosity, "turbulentViscosity", kContext);
  requirePositive(density, "density", kContext);
  requirePositive(turbulentPrandtl, "turbulentPrandtl", kContext);
  return turbulentViscosity / (density * turbulentPrandtl);
}

ScalarField effectiveConductivityField(const Mesh& mesh, const ScalarField& molecularConductivity,
                                       const ScalarField& turbulentViscosity,
                                       const ScalarField& specificHeat, Real turbulentPrandtl) {
  constexpr const char* kContext = "effectiveConductivityField";
  const Index n = mesh.numberOfCells();
  if (molecularConductivity.size() != n || turbulentViscosity.size() != n ||
      specificHeat.size() != n) {
    throw InvalidArgumentError(
        "effectiveConductivityField: molecularConductivity/turbulentViscosity/specificHeat "
        "size does not match mesh cell count");
  }
  requirePositive(turbulentPrandtl, "turbulentPrandtl", kContext);

  ScalarField effective(n);
  for (Index i = 0; i < n; ++i) {
    requirePositive(molecularConductivity[i], "molecularConductivity", kContext);
    // turbulentConductivity re-validates mu_t and cp per cell, so a single bad
    // entry is reported where it is rather than producing a silently wrong
    // field. The cost is one branch per cell in a loop that already touches
    // three fields.
    effective[i] = molecularConductivity[i] +
                   turbulentConductivity(turbulentViscosity[i], specificHeat[i], turbulentPrandtl);
  }
  return effective;
}

}  // namespace cfd::thermal
