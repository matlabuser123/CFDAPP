#include "cfd/turbulence/TurbulenceProduction.hpp"

#include <algorithm>

#include "cfd/core/Exception.hpp"

namespace cfd::turbulence {

using cfd::discretization::VelocityGradientField;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

ScalarField computeStrainRateMagnitudeSquared(const Mesh& mesh,
                                              const VelocityGradientField& velocityGradient) {
  const Index n = mesh.numberOfCells();
  if (velocityGradient.gradU.size() != n || velocityGradient.gradV.size() != n) {
    throw InvalidArgumentError(
        "computeStrainRateMagnitudeSquared: gradU/gradV size does not match mesh cell count");
  }

  ScalarField s2(n);
  for (Index i = 0; i < n; ++i) {
    const Real dudx = velocityGradient.gradU[i].x;
    const Real dudy = velocityGradient.gradU[i].y;
    const Real dvdx = velocityGradient.gradV[i].x;
    const Real dvdy = velocityGradient.gradV[i].y;
    const Real shear = dudy + dvdx;
    s2[i] = (2.0 * dudx * dudx) + (2.0 * dvdy * dvdy) + (shear * shear);
  }
  return s2;
}

ScalarField computeTurbulentProduction(const Mesh& mesh, const ScalarField& turbulentViscosity,
                                       const VelocityGradientField& velocityGradient) {
  const Index n = mesh.numberOfCells();
  if (turbulentViscosity.size() != n || velocityGradient.gradU.size() != n ||
      velocityGradient.gradV.size() != n) {
    throw InvalidArgumentError(
        "computeTurbulentProduction: turbulentViscosity/gradU/gradV size does not match mesh "
        "cell count");
  }

  const ScalarField s2 = computeStrainRateMagnitudeSquared(mesh, velocityGradient);
  ScalarField production(n);
  for (Index i = 0; i < n; ++i) {
    production[i] = turbulentViscosity[i] * s2[i];
  }
  return production;
}

ScalarField computeTurbulentViscosityField(const Mesh& mesh, Real density, Real cMu,
                                           const ScalarField& k, const ScalarField& epsilon,
                                           Real kFloor, Real epsilonFloor) {
  const Index n = mesh.numberOfCells();
  ScalarField muT(n);
  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k[i], kFloor);
    const Real epsSafe = std::max(epsilon[i], epsilonFloor);
    muT[i] = density * cMu * kSafe * kSafe / epsSafe;
  }
  return muT;
}

}  // namespace cfd::turbulence
