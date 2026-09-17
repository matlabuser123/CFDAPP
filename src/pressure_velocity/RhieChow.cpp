#include "cfd/pressure_velocity/RhieChow.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

namespace cfd::pressure_velocity {

using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

SurfaceField rhieChowFaceCorrection(const Mesh& mesh, const ScalarField& pressure,
                                    const VectorField& pressureGradient, const ScalarField& dU,
                                    const ScalarField& dV, const ScalarField* dW, Real density,
                                    Real alpha) {
  const Index n = mesh.numberOfCells();
  if (pressure.size() != n || pressureGradient.size() != n || dU.size() != n || dV.size() != n ||
      (dW != nullptr && dW->size() != n)) {
    throw InvalidArgumentError("rhieChowFaceCorrection: field size does not match mesh cell count");
  }
  if (mesh.dimension() == 3 && dW == nullptr) {
    throw InvalidArgumentError("rhieChowFaceCorrection: a 3D mesh needs the w response dW");
  }
  if (!std::isfinite(alpha) || !(alpha > 0.0) || alpha > 1.0) {
    throw InvalidArgumentError("rhieChowFaceCorrection: alpha must be finite and in (0, 1]");
  }

  SurfaceField correction(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    // D_f: the two-point pressure-correction coupling of this face (the same
    // coefficient the N = 0 pressure-correction equation uses).
    const Real coupling =
        pressureCorrectionFaceCoupling(mesh, face, density, dU, dV, /*nonOrthogonal=*/false, dW)
            .coefficient;
    const Vector3 d = mesh.cell(neighborId).centroid() - mesh.cell(ownerId).centroid();
    const Vector3 gradFace =
        cfd::discretization::interpolateInternalFace(mesh, face, pressureGradient);
    const Real compactMinusInterpolated =
        (pressure[neighborId] - pressure[ownerId]) - dot(gradFace, d);
    correction[face.id()] = -(coupling / alpha) * compactMinusInterpolated;
  }
  return correction;
}

SurfaceField rhieChowMassFlux(const Mesh& mesh, const VectorField& velocityStar,
                              const ScalarField& pressure, const VectorField& pressureGradient,
                              const ScalarField& dU, const ScalarField& dV, const ScalarField* dW,
                              const cfd::physics::FluidProperties& fluid,
                              const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                              Real alpha) {
  SurfaceField flux =
      cfd::physics::calculateMassFlux(mesh, velocityStar, fluid, velocityBoundaries);
  const SurfaceField correction =
      rhieChowFaceCorrection(mesh, pressure, pressureGradient, dU, dV, dW, fluid.density(), alpha);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) flux[f] += correction[f];
  return flux;
}

}  // namespace cfd::pressure_velocity
