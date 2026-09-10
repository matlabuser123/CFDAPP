#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::turbulence {

// P2-TURB-006 sections 9-10: for every cell, the minimum Euclidean
// distance from the cell centroid to the centroid of any boundary face
// whose assigned *velocity* boundary condition is a wall
// (BoundaryConditionType::Wall or MovingWall) -- "wall" is determined by
// reusing the model's own already-existing velocityBoundaries (the same
// BoundaryConditionSet cfd::turbulence::KOmegaModel-style models already
// take), not a separate wall-marking config; inlet/outlet/symmetry
// patches are never mistaken for walls this way, since only their
// assigned condition *type* is inspected, nothing else.
//
// Face-centroid-to-cell-centroid distance, not a true point-to-plane
// projection: exact on the structured Cartesian meshes this codebase
// currently supports (the nearest wall face is always the one directly
// across the cell in its own row/column, whose centroid sits at the
// exact perpendicular foot -- verified independently in
// WallDistanceTest's own hand-derived cases), and this task's own scope
// (section 9) only requires correctness "on structured Cartesian
// meshes", not a general-mesh point-to-plane distance transform.
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
