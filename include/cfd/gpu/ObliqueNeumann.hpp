#pragma once

// GPU-DISC-001 -- host-side restatement of Gradient.cpp's file-static
// `obliqueNeumannFace` (P12-MESH-001, Gradient.cpp:190).
//
// Extracted from DeviceGradientPlan.cpp (GPU-DISC-001B) when the least-squares
// gradient plan (GPU-DISC-001J) needed the same predicate. Moved, not copied:
// the whole point of the restatement is that it agrees with Gradient.cpp
// expression for expression, and two copies are exactly the thing that drifts
// apart silently. 001B's 132-case bitwise differential is re-run after the move
// to show its behaviour is unchanged.
//
// `unitNormal` is carried here even though the Green-Gauss path only consumes
// the tangential offset: the least-squares path's displacement for an oblique
// face is `unitNormal * normalDistance`, and re-deriving it at the call site
// would reintroduce the drift this header exists to prevent.
//
// The drift risk is real and is covered by the differentials, which include
// meshes where the predicate is live. If this ever disagrees with Gradient.cpp,
// those cases stop being bitwise equal and fail.

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::gpu {

struct ObliqueFace {
  bool applies{false};
  cfd::Real normalDistance{0.0};
  cfd::Vector2 unitNormal{0.0, 0.0};
  cfd::Vector2 tangentialOffset{0.0, 0.0};
};

// Same expressions, same rejection order, as Gradient.cpp:190.
inline ObliqueFace obliqueNeumannFaceRestated(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
    const cfd::boundary::BoundaryConditionSet& boundaries) {
  using cfd::Real;
  using cfd::Vector2;
  using cfd::Vector3;
  using cfd::mesh::MeshGeometry;
  if (cfd::discretization::prescribesBoundaryValue(
          cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries).type())) {
    return {};
  }
  const Vector2 d = face.centroid() - mesh.cell(face.owner()).centroid();
  const Vector2 n = MeshGeometry::unitNormal(face);
  const Real dn = dot(d, n);
  if (!(dn > 0.0)) return {};
  const Vector2 tangentialOffset = d - (n * dn);
  if (tangentialOffset == Vector3{}) return {};
  return {true, dn, n, tangentialOffset};
}

}  // namespace cfd::gpu
