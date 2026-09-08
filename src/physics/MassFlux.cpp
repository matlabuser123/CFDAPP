#include "cfd/physics/MassFlux.hpp"

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"

namespace cfd::physics {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

SurfaceField calculateMassFlux(const Mesh& mesh, const VectorField& velocity,
                               const FluidProperties& fluid,
                               const BoundaryConditionSet& velocityBoundaries) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("calculateMassFlux: velocity size does not match mesh cell count");
  }

  SurfaceField massFlux(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const auto& face = mesh.face(faceId);
    // interpolateFace already dispatches to linear interpolation on
    // internal faces and to the boundary condition's velocity on
    // boundary faces (Wall -> 0, MovingWall/Inlet -> prescribed,
    // Outlet -> owner value, Symmetry -> normal component removed) --
    // see cfd/discretization/Interpolation.hpp. No per-BC-type logic is
    // duplicated here.
    const Vector2 faceVelocity =
        cfd::discretization::interpolateFace(mesh, face, velocity, velocityBoundaries);
    massFlux[faceId] = fluid.density() * dot(faceVelocity, face.areaVector());
  }
  return massFlux;
}

}  // namespace cfd::physics
