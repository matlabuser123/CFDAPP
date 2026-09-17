#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::turbulence {

// P2-TURB-006 sections 9-10: for every cell, the minimum Euclidean
// distance from the cell centroid to any boundary face (P12-MESH-001: to
// the face's straight segment, not its centroid -- see below)
// whose assigned *velocity* boundary condition is a wall
// (BoundaryConditionType::Wall or MovingWall) -- "wall" is determined by
// reusing the model's own already-existing velocityBoundaries (the same
// BoundaryConditionSet cfd::turbulence::KOmegaModel-style models already
// take), not a separate wall-marking config; inlet/outlet/symmetry
// patches are never mistaken for walls this way, since only their
// assigned condition *type* is inspected, nothing else.
//
// P12-MESH-001: the distance to each wall face is the exact point-to-
// segment distance (the face is the straight edge centroid +/- half its
// area along its tangent), so the minimum over a straight wall's faces is
// the exact perpendicular wall distance on any mesh. The pre-existing
// face-centroid distance was exact only when the nearest wall face's
// centroid sits at the perpendicular foot (every structured Cartesian
// mesh); on a structured_quad mesh whose grid lines meet the wall
// obliquely it overestimated wall-adjacent distances by 22-24% (measured,
// results/p12-mesh-001/summary.md), a relative error that does not shrink
// under refinement. On a Cartesian mesh the nearest face's projection
// parameter is exactly 0, so the result is bit-identical to before
// (WallDistanceTest's hand-derived cases).
//
// Computed once, not on every RANS iteration (P2-TURB-006 section 58:
// "for fixed meshes, wall distance can be computed once") -- the mesh
// and its wall patches never change during a solve, so
// cfd::turbulence::SSTModel calls this exactly once, at construction,
// and caches the result.
//
// Throws InvalidArgumentError if no patch's assigned velocity condition
// is Wall/MovingWall (SST's near-wall blending is meaningless without at
// least one wall -- silently returning some arbitrary sentinel distance
// would misrepresent the model's own state), or if any resulting
// distance is not finite and > 0 (a defensive check: this should never
// happen for a well-formed finite-volume mesh, since cell centroids are
// always strictly interior, but is checked explicitly rather than
// assumed, matching this codebase's "validate at the point of entry"
// convention).
[[nodiscard]] cfd::fields::ScalarField computeWallDistance(
    const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& velocityBoundaries);

}  // namespace cfd::turbulence
