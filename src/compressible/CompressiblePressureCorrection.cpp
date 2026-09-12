#include "cfd/compressible/CompressiblePressureCorrection.hpp"

#include <cmath>
#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

namespace cfd::compressible {

using cfd::algebra::LinearSystem;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::boundaryConditionForFace;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::evaluateContinuity;
using cfd::pressure_velocity::PressureCorrectionAssembly;

namespace {

// Same P0-scope restriction as PressureCorrectionEquation.cpp's own --
// this phase's structured Cartesian mesh only.
bool isXNormalFace(const Face& face) {
  const Vector2& sf = face.areaVector();
  const bool xNormal = (sf.y == 0.0) && (sf.x != 0.0);
  const bool yNormal = (sf.x == 0.0) && (sf.y != 0.0);
  if (!xNormal && !yNormal) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: face is not axis-aligned (outside this phase's "
        "structured Cartesian scope)");
  }
  return xNormal;
}

}  // namespace

PressureCorrectionAssembly assembleCompressiblePressureCorrection(
    const Mesh& mesh, const SurfaceField& predictorMassFlux, const SurfaceField& faceDensity,
    const ScalarField& uResponseCoefficient, const ScalarField& vResponseCoefficient,
    const ScalarField& pressureAbsolute, const ScalarField& temperature,
    const ThermodynamicProperties& thermodynamics, Real pseudoTimeStep, Index referenceCell,
    const BoundaryConditionSet& pressureBoundaries) {
  const Index n = mesh.numberOfCells();
  if (predictorMassFlux.size() != mesh.numberOfFaces() ||
      faceDensity.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: predictorMassFlux/faceDensity size does not "
        "match mesh face count");
  }
  if (uResponseCoefficient.size() != n || vResponseCoefficient.size() != n ||
      pressureAbsolute.size() != n || temperature.size() != n) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: field size does not match mesh cell count");
  }
  if (!std::isfinite(pseudoTimeStep) || !(pseudoTimeStep > 0.0)) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: pseudoTimeStep must be finite and > 0");
  }
  if (referenceCell >= n) {
    throw InvalidArgumentError("assembleCompressiblePressureCorrection: referenceCell out of range");
  }

  // Same "a Dirichlet pressure patch already removes the null space"
  // determination as assemblePressureCorrection's own -- see that
  // function's header comment. Note: with the new compressibility
  // diagonal term below, the system is no longer singular even on a
  // fully-closed (all-Neumann) domain (dDensityDPressure > 0 gives every
  // row a strictly positive diagonal contribution on its own) -- but
  // `referenceCell` pinning is retained anyway for exact behavioral
  // continuity with the incompressible equation's own convention, and
  // because the compressibility term can be numerically tiny (large
  // pseudoTimeStep or near-incompressible gas) where the pin still
  // matters for conditioning.
  bool hasOpenBoundary = false;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() == BoundaryConditionType::FixedValue) {
      hasOpenBoundary = true;
      break;
    }
  }
  const bool pinReferenceCell = !hasOpenBoundary;

  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  SurfaceField faceCoefficient(mesh.numberOfFaces(), 0.0);

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    // P12-COMP-002: faceDensity[faceId] is already the correct per-face
    // value (evaluateCompressibleFaceDensity: internal-face interpolation
    // or P12-COMP-001's boundary EOS treatment) -- unlike
    // assemblePressureCorrection's own single global `density`, no
    // further interpolation/owner-value logic is needed here.
    if (face.isBoundary()) {
      const auto& bc = boundaryConditionForFace(mesh, faceId, pressureBoundaries);
      if (bc.type() != BoundaryConditionType::FixedValue) {
        continue;  // Neumann-like: zero coupling -- see the header comment.
      }
      const Index ownerId = face.owner();
      const ScalarField& response =
          isXNormalFace(face) ? uResponseCoefficient : vResponseCoefficient;
      const Real hP = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Real dCoefficient = faceDensity[faceId] * face.area() * response[ownerId] / hP;
      faceCoefficient[faceId] = dCoefficient;
      if (!pinReferenceCell || ownerId != referenceCell) {
        builder.add(ownerId, ownerId, dCoefficient);
      }
      continue;
    }

    const ScalarField& response = isXNormalFace(face) ? uResponseCoefficient : vResponseCoefficient;
    const Real dFace = cfd::discretization::interpolateInternalFace(mesh, face, response);
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real dCoefficient = faceDensity[faceId] * face.area() * dFace / dPN;
    faceCoefficient[faceId] = dCoefficient;

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
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

  // P12-COMP-002: the new compressibility diagonal term -- this equation
  // discretizes d(rho)/dt (via dDensityDPressure * dp/dt, implicit in the
  // unknown pressure correction) against the pseudo-time-step. Skipped
  // for the pinned reference cell's own row, same as every D_f
  // contribution above -- SparseMatrixBuilder::add *sums* into an
  // existing entry rather than overwriting it (see UnderRelaxation.hpp's
  // own comment on this), so adding here unconditionally and then
  // "overwriting" with the identity pin below would actually leave
  // 1.0 + (this term) on that row, not exactly 1.0.
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    if (pinReferenceCell && id == referenceCell) continue;
    const Real dRhoDp = thermodynamics.equationOfState().dDensityDPressure(pressureAbsolute[id],
                                                                           temperature[id]);
    builder.add(id, id, cell.volume() / pseudoTimeStep * dRhoDp);
  }

  if (pinReferenceCell) {
    builder.add(referenceCell, referenceCell, 1.0);
    rhs[referenceCell] = 0.0;
  }

  SparseMatrix matrix = builder.build();
  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleCompressiblePressureCorrection: assembled system contains a non-finite value");
  }

  return PressureCorrectionAssembly{LinearSystem(std::move(matrix), std::move(rhs)),
                                    std::move(faceCoefficient)};
}

}  // namespace cfd::compressible
