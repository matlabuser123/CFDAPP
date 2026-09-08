#include "cfd/discretization/Gradient.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Cell;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// Plain Green-Gauss gradient mixes an *exact* boundary value (zero error)
// with an *interpolated* opposite-face value (O(h^2) error) in the same
// central-difference-shaped sum. That mismatch -- not any asymmetry in
// face positions -- leaves an O(h) bias in the boundary cell's own
// gradient (confirmed by Taylor expansion: the boundary-adjacent
// component drops from the interior's clean 2nd order to 1st order).
// This is a known limitation of plain Green-Gauss gradients at
// boundaries. Where an interior neighbor sits directly opposite the
// boundary face (true for every non-degenerate cell on an orthogonal
// Cartesian mesh), replace the {boundary face, opposite face}
// contribution pair with the exact directional derivative from a
// quadratic fit through (boundary, owner, opposite neighbor) -- second
// order in general, exact for quadratics, matching the interior scheme.
struct PairedBoundaryContribution {
  bool applies = false;
  Index oppositeFaceId = 0;
  Vector2 contribution{0.0, 0.0};
};

PairedBoundaryContribution tryPairedBoundaryContribution(const Mesh& mesh, const Cell& cell,
                                                         const Face& boundaryFace,
                                                         const ScalarField& field,
                                                         const BoundaryConditionSet& boundaries) {
  const auto oppositeFaceId = MeshGeometry::oppositeInteriorFace(mesh, cell, boundaryFace);
  if (!oppositeFaceId.has_value()) {
    return {};
  }
  const Face& oppositeFace = mesh.face(*oppositeFaceId);

  // The shortcut below assumes the boundary face and its opposite share
  // the same area (exactly true for an orthogonal Cartesian quad cell,
  // where both faces bound the same row/column width). If a future mesh
  // breaks that, fall back to the ordinary per-face treatment rather
  // than silently misapplying the formula.
  if (std::abs(boundaryFace.area() - oppositeFace.area()) > 1e-12 * boundaryFace.area()) {
    return {};
  }

  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, boundaryFace.id(), boundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("gradient: boundary condition is not scalar-valued");
  }

  const Real h1 = MeshGeometry::distance(cell.centroid(), boundaryFace.centroid());
  const Real h2 = MeshGeometry::ownerNeighborDistance(mesh, oppositeFace);
  const Index farCellId =
      (oppositeFace.owner() == cell.id()) ? *oppositeFace.neighbor() : oppositeFace.owner();

  const Real phiP = field[cell.id()];
  const Real phiB = scalarBc->boundaryValue(phiP, h1);
  const Real phiN = field[farCellId];

  // Three-point one-sided derivative at x1=P for points (B, P, N) at
  // relative positions (0, h1, h1+h2): exact for quadratics, O(h^2) in
  // general -- see docs/architecture or TODO.md P0 gate notes.
  const Real a = -h2 / (h1 * (h1 + h2));
  const Real b = (h2 - h1) / (h1 * h2);
  const Real c = h1 / (h2 * (h1 + h2));
  const Real dPhiDInward = (a * phiB) + (b * phiP) + (c * phiN);

  // grad along outward normal = -dPhiDInward. The caller divides the
  // whole per-cell sum by cell.volume() exactly once at the end (summed
  // together with every other face's contribution), so what we hand
  // back here must already be "would-be gradient * cell.volume()" --
  // NOT scaled by face area or by h1+h2, which are unrelated to the
  // cell's actual volume.
  const Vector2 outwardNormal = MeshGeometry::unitNormal(boundaryFace);
  const Vector2 contribution = outwardNormal * (-dPhiDInward * cell.volume());

  return {true, *oppositeFaceId, contribution};
}

}  // namespace

VectorField gradient(const Mesh& mesh, const ScalarField& field,
                     const BoundaryConditionSet& boundaries) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("gradient: field size does not match mesh cell count");
  }

  const SurfaceField faceValues = interpolate(mesh, field, boundaries);

  VectorField result(mesh.numberOfCells(), Vector2{0.0, 0.0});
  for (const auto& cell : mesh.cells()) {
    Vector2 sum{0.0, 0.0};

    // First pass: pair up every boundary face of this cell with its
    // opposite interior neighbor (if any) up front, before any standard
    // per-face contribution is added. Doing this in a separate pass --
    // rather than inline, in cell.faceIds() order -- avoids double-
    // counting the opposite face: face insertion order depends on which
    // side of the cell the boundary sits on (see MeshGeometry's
    // structured generator), so the opposite face is not reliably
    // visited *after* its boundary face in a single pass.
    std::vector<Index> handledFaceIds;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      if (!face.isBoundary()) {
        continue;
      }
      const PairedBoundaryContribution paired =
          tryPairedBoundaryContribution(mesh, cell, face, field, boundaries);
      if (paired.applies) {
        sum += paired.contribution;
        handledFaceIds.push_back(faceId);
        handledFaceIds.push_back(paired.oppositeFaceId);
      }
    }

    // Second pass: standard treatment for every face not already folded
    // into a paired contribution above.
    for (const Index faceId : cell.faceIds()) {
      if (std::find(handledFaceIds.begin(), handledFaceIds.end(), faceId) != handledFaceIds.end()) {
        continue;
      }
      const auto& face = mesh.face(faceId);
      const Vector2 sfCell =
          (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
      sum += sfCell * faceValues[faceId];
    }

    result[cell.id()] = sum * (1.0 / cell.volume());
  }
  return result;
}

}  // namespace cfd::discretization
