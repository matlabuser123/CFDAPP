#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

#include <cmath>
#include <memory>
#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::LinearSystem;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::boundaryConditionForFace;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::evaluateContinuity;

namespace {

// This mesh is restricted to axis-aligned Cartesian faces (P0 scope):
// exactly one component of the area vector is zero. Throws if a face
// doesn't fit that (defends against this code silently misapplying to a
// future non-orthogonal mesh).
bool isXNormalFace(const Face& face) {
  const Vector2& sf = face.areaVector();
  const bool xNormal = (sf.y == 0.0) && (sf.x != 0.0);
  const bool yNormal = (sf.x == 0.0) && (sf.y != 0.0);
  if (!xNormal && !yNormal) {
    throw InvalidArgumentError(
        "assemblePressureCorrection: face is not axis-aligned (outside this phase's structured "
        "Cartesian scope)");
  }
  return xNormal;
}

// p' boundary condition for the gradient used by correctVelocity, per
// patch: FixedValue(0.0) where the actual pressure patch is FixedValue
// (Dirichlet, p'=0 there -- matching assemblePressureCorrection's
// treatment), FixedGradient(0.0) everywhere else.
BoundaryConditionSet makeGradientBoundaries(const Mesh& mesh,
                                            const BoundaryConditionSet& pressureBoundaries) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() == BoundaryConditionType::FixedValue) {
      boundaries.set(mesh, patch.name(), std::make_unique<FixedValue>(0.0));
    } else {
      boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
    }
  }
  return boundaries;
}

}  // namespace

ScalarField computeMomentumResponseCoefficient(const Mesh& mesh, const Vector& momentumDiagonal) {
  if (momentumDiagonal.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "computeMomentumResponseCoefficient: momentumDiagonal size does not match mesh cell "
        "count");
  }
  ScalarField d(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    d[cell.id()] = cell.volume() / momentumDiagonal[cell.id()];
  }
  return d;
}

PressureCorrectionAssembly assemblePressureCorrection(
    const Mesh& mesh, const SurfaceField& predictorMassFlux,
    const ScalarField& uResponseCoefficient, const ScalarField& vResponseCoefficient, Real density,
    Index referenceCell, const BoundaryConditionSet& pressureBoundaries) {
  const Index n = mesh.numberOfCells();
  if (predictorMassFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assemblePressureCorrection: predictorMassFlux size does not match mesh face count");
  }
  if (uResponseCoefficient.size() != n || vResponseCoefficient.size() != n) {
    throw InvalidArgumentError(
        "assemblePressureCorrection: response coefficient size does not match mesh cell count");
  }
  if (!std::isfinite(density) || !(density > 0.0)) {
    throw InvalidArgumentError("assemblePressureCorrection: density must be finite and > 0");
  }
  if (referenceCell >= n) {
    throw InvalidArgumentError("assemblePressureCorrection: referenceCell out of range");
  }

  // A FixedValue (Dirichlet) pressure patch anywhere already removes the
  // pressure-correction null space physically -- forcing `referenceCell`
  // to zero as well would over-constrain an already well-posed system
  // (see header comment). Determined up front so the per-face loop below
  // can decide, once, whether referenceCell's own row will be forced
  // afterward.
  bool hasOpenBoundary = false;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() == BoundaryConditionType::FixedValue) {
      hasOpenBoundary = true;
      break;
    }
  }
  const bool pinReferenceCell = !hasOpenBoundary;

  SparseMatrixBuilder builder(n, n);
  SurfaceField faceCoefficient(mesh.numberOfFaces(), 0.0);

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    if (face.isBoundary()) {
      const auto& bc = boundaryConditionForFace(mesh, faceId, pressureBoundaries);
      if (bc.type() != BoundaryConditionType::FixedValue) {
        continue;  // Neumann-like: zero coupling -- see the header comment.
      }
      // Dirichlet (fixed-pressure) boundary: F_f' = D_f * (p'_P - 0),
      // using the owner cell's own response coefficient (no
      // interpolation -- there is no neighbor cell) and the distance
      // from the owner centroid to the boundary face.
      const Index ownerId = face.owner();
      const ScalarField& response =
          isXNormalFace(face) ? uResponseCoefficient : vResponseCoefficient;
      const Real hP = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Real dCoefficient = density * face.area() * response[ownerId] / hP;
      faceCoefficient[faceId] = dCoefficient;
      if (!pinReferenceCell || ownerId != referenceCell) {
        builder.add(ownerId, ownerId, dCoefficient);
      }
      continue;
    }

    const ScalarField& response = isXNormalFace(face) ? uResponseCoefficient : vResponseCoefficient;
    const Real dFace = cfd::discretization::interpolateInternalFace(mesh, face, response);
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real dCoefficient = density * face.area() * dFace / dPN;
    faceCoefficient[faceId] = dCoefficient;

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();

    // Face-once assembly (TODO.md section 51): both rows' equal/opposite
    // contributions added from this single face visit -- except the
    // reference cell's own row, which (when pinReferenceCell) is left
    // untouched here and forced to the identity equation below
    // (SparseMatrixBuilder only ever sums entries, so its row can't be
    // "added then overwritten").
    if (!pinReferenceCell || ownerId != referenceCell) {
      builder.add(ownerId, ownerId, dCoefficient);
      builder.add(ownerId, neighborId, -dCoefficient);
    }
    if (!pinReferenceCell || neighborId != referenceCell) {
      builder.add(neighborId, neighborId, dCoefficient);
      builder.add(neighborId, ownerId, -dCoefficient);
    }
  }

  const auto continuity = evaluateContinuity(mesh, predictorMassFlux);
  Vector rhs(n);
  for (const auto& cell : mesh.cells()) {
    rhs[cell.id()] = -continuity.cellImbalance[cell.id()];
  }

  if (pinReferenceCell) {
    builder.add(referenceCell, referenceCell, 1.0);
    rhs[referenceCell] = 0.0;
  }

  SparseMatrix matrix = builder.build();
  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assemblePressureCorrection: assembled system contains a non-finite value");
  }

  return PressureCorrectionAssembly{LinearSystem(std::move(matrix), std::move(rhs)),
                                    std::move(faceCoefficient)};
}

SurfaceField correctFaceMassFlux(const Mesh& mesh, const SurfaceField& predictorMassFlux,
                                 const SurfaceField& faceCoefficient,
                                 const ScalarField& pressureCorrection) {
  if (predictorMassFlux.size() != mesh.numberOfFaces() ||
      faceCoefficient.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("correctFaceMassFlux: field size does not match mesh face count");
  }
  if (pressureCorrection.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "correctFaceMassFlux: pressureCorrection size does not match mesh cell count");
  }

  SurfaceField corrected(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real pOwner = pressureCorrection[face.owner()];
    // Internal face: F_f' = D_f*(p'_owner - p'_neighbor). Boundary face:
    // F_f' = D_f*(p'_owner - 0), the Dirichlet-pressure-patch term from
    // assemblePressureCorrection -- faceCoefficient is exactly 0 for
    // every other boundary face, making this a no-op there.
    const Real pNeighbor = face.isBoundary() ? 0.0 : pressureCorrection[*face.neighbor()];
    const Real fluxCorrection = faceCoefficient[faceId] * (pOwner - pNeighbor);
    corrected[faceId] = predictorMassFlux[faceId] + fluxCorrection;
  }
  return corrected;
}

VectorField correctVelocity(const Mesh& mesh, const VectorField& predictorVelocity,
                            const ScalarField& uResponseCoefficient,
                            const ScalarField& vResponseCoefficient,
                            const ScalarField& pressureCorrection,
                            const BoundaryConditionSet& pressureBoundaries) {
  const Index n = mesh.numberOfCells();
  if (predictorVelocity.size() != n || uResponseCoefficient.size() != n ||
      vResponseCoefficient.size() != n || pressureCorrection.size() != n) {
    throw InvalidArgumentError("correctVelocity: field size does not match mesh cell count");
  }

  const BoundaryConditionSet boundaries = makeGradientBoundaries(mesh, pressureBoundaries);
  const VectorField gradPPrime =
      cfd::discretization::gradient(mesh, pressureCorrection, boundaries);

  VectorField corrected(n);
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    corrected[id] =
        Vector2{predictorVelocity[id].x - (uResponseCoefficient[id] * gradPPrime[id].x),
                predictorVelocity[id].y - (vResponseCoefficient[id] * gradPPrime[id].y)};
  }
  return corrected;
}

}  // namespace cfd::pressure_velocity
