// GPU-DISC-001B -- host-side construction of the immutable gradient plan.
//
// Deliberately plain host code that calls the SAME MeshGeometry and boundary
// functions the CPU gradient calls. Nothing here re-derives geometry with its
// own formula: every immutable quantity the device will use is produced by the
// production implementation, so the device cannot disagree with the CPU about
// the mesh.
//
// Two predicates ARE restated rather than called, because Gradient.cpp keeps
// them in an anonymous namespace: `obliqueNeumannFace` (Gradient.cpp:190) and
// `boundaryTransferNeeded` (Gradient.cpp:449). Both are reproduced expression
// for expression and marked as such. The drift risk that creates is real and is
// covered by the differential test, which includes meshes where each predicate
// is live -- an oblique-Neumann mesh and a mesh whose fit lines meet the
// boundary off-centroid. If either restatement ever disagrees with Gradient.cpp,
// those cases stop being bitwise equal and fail.
//
// GPU-DISC-001J moved the oblique restatement into ObliqueNeumann.hpp, shared
// with the least-squares gradient plan, so there is still exactly one copy.
//
// See results/gpu-disc-001/gradients/audit.md for why this split exists.

#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/gpu/BoundaryEncoding.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/gpu/DeviceGradient.hpp"
#include "cfd/gpu/ObliqueNeumann.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::Vector3;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// obliqueNeumannFaceRestated now lives in ObliqueNeumann.hpp (shared with the
// least-squares gradient plan).

// Restatement of Gradient.cpp's file-static boundaryTransferNeeded
// (P12-GRAD-002). Note it does NOT require a valid owner/neighbour crossing --
// the claim predicate does, this one does not -- so it is deliberately not
// folded into the claim loop below.
bool boundaryTransferNeededRestated(const Mesh& mesh) {
  for (const auto& cell : mesh.cells()) {
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      if (!face.isBoundary()) continue;
      const auto oppositeFaceId = MeshGeometry::oppositeInteriorFace(mesh, cell, face);
      if (!oppositeFaceId.has_value()) continue;
      const Face& oppositeFace = mesh.face(*oppositeFaceId);
      const Index farCellId =
          (oppositeFace.owner() == cell.id()) ? *oppositeFace.neighbor() : oppositeFace.owner();
      const Vector2 d = mesh.cell(farCellId).centroid() - cell.centroid();
      const Real farDistance = magnitude(d);
      if (!(farDistance > 0.0)) continue;
      const auto intersection =
          MeshGeometry::boundaryLineIntersection(mesh, face, d * (1.0 / farDistance));
      if (intersection.valid && (intersection.point - face.centroid()) != Vector3{}) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

bool DeviceGradientPlan::build(const Mesh& mesh,
                               const cfd::boundary::BoundaryConditionSet& boundaries) {
  usable_ = false;
  sweepsNeeded_ = false;
  boundaryTransfer_ = false;
  unsupportedReason_.clear();
  mesh_.upload(mesh);

  const Index nc = static_cast<Index>(mesh.numberOfCells());
  const Index nf = static_cast<Index>(mesh.numberOfFaces());

  // --- interior interpolation distances -----------------------------------
  std::vector<Real> dPf(nf, 0.0), dNf(nf, 0.0);
  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    if (face.isBoundary()) continue;
    dPf[f] = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
    dNf[f] = MeshGeometry::distance(mesh.cell(*face.neighbor()).centroid(), face.centroid());
  }

  // --- boundary conditions, encoded and verified ---------------------------
  // Two encodings per oblique face: interpolate() gives the INITIAL value using
  // the straight-line distance, and the sweeps re-evaluate oblique faces using
  // d.n. Conflating the two would be a real discretization change.
  std::vector<Real> bA(nf, 0.0), bB(nf, 0.0);
  std::vector<Index> bKind(nf, kBoundaryEncodingConstant);
  std::vector<Index> obliqueIds, oblKind;
  std::vector<Real> oblA, oblB, oblTx, oblTy, oblTz;
  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    if (!face.isBoundary()) continue;
    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
    const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
    if (scalarBc == nullptr) {
      unsupportedReason_ = "face " + std::to_string(f) + ": boundary condition is not scalar-valued";
      return false;
    }
    const Real distance =
        MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
    if (!encodeBoundaryCondition(*scalarBc, distance, bA[f], bB[f], bKind[f])) {
      unsupportedReason_ = "face " + std::to_string(f) + ": boundary condition '" +
                           std::string(bc.name()) +
                           "' is not bitwise-reproducible as a constant, a shift or an affine "
                           "function of phi_P; mesh rejected for the device gradient path";
      return false;
    }
    const ObliqueFace oblique = obliqueNeumannFaceRestated(mesh, face, boundaries);
    if (!oblique.applies) continue;
    Real a = 0.0, b = 0.0;
    Index kind = kBoundaryEncodingConstant;
    if (!encodeBoundaryCondition(*scalarBc, oblique.normalDistance, a, b, kind)) {
      unsupportedReason_ = "face " + std::to_string(f) + ": oblique-Neumann condition '" +
                           std::string(bc.name()) +
                           "' is not bitwise-reproducible at the normal distance";
      return false;
    }
    obliqueIds.push_back(f);
    oblKind.push_back(kind);
    oblA.push_back(a);
    oblB.push_back(b);
    oblTx.push_back(oblique.tangentialOffset.x);
    oblTy.push_back(oblique.tangentialOffset.y);
    oblTz.push_back(oblique.tangentialOffset.z);
  }

  // --- P12-NUM-003 skewed faces -------------------------------------------
  std::vector<Index> skewIds;
  std::vector<Real> skewX, skewY, skewZ, skewT;
  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, face);
    if (!crossing.has_value() || crossing->skewVector == Vector3{}) continue;
    skewIds.push_back(f);
    // The skew-corrected value is
    //   phi_x + dot(grad_x, skewVector),  x = owner + t*(neighbor - owner)
    // (Interpolation.cpp:61) -- so it is the SKEW VECTOR that is needed here,
    // not the crossing point.
    skewX.push_back(crossing->skewVector.x);
    skewY.push_back(crossing->skewVector.y);
    skewZ.push_back(crossing->skewVector.z);
    skewT.push_back(crossing->t);
  }

  // --- P12-GRAD-002 boundary-consistent claims -----------------------------
  // Reproduces greenGaussSweep's per-cell claim collection and its conflict
  // resolution -- min by (antiParallelAlignment, claiming boundary face id) --
  // both of which depend only on the mesh.
  std::vector<Index> cCell, cTarget, cBoundary, cFar;
  std::vector<Real> cFarDist, cBackDist, cW, cOffX, cOffY, cOffZ;
  // Claims are emitted in increasing cell order, so each cell's claims are a
  // contiguous run. Recording where each run starts keeps the slot->claim map
  // below linear instead of nc * claimCount.
  std::vector<Index> claimRowBegin(nc + 1, 0);
  std::vector<std::tuple<Index, Index, Real, Index>> perCell;  // target, bface, align, claimIdx

  for (Index cellId = 0; cellId < nc; ++cellId) {
    claimRowBegin[cellId] = static_cast<Index>(cCell.size());
    const auto& cell = mesh.cell(cellId);
    perCell.clear();
    for (const Index faceId : cell.faceIds()) {
      const Face& bf = mesh.face(faceId);
      if (!bf.isBoundary()) continue;

      const auto oppositeId = MeshGeometry::oppositeInteriorFace(mesh, cell, bf);
      if (!oppositeId.has_value()) continue;
      const Face& opposite = mesh.face(*oppositeId);
      const Index farCellId =
          (opposite.owner() == cell.id()) ? *opposite.neighbor() : opposite.owner();

      const Vector2 d = mesh.cell(farCellId).centroid() - cell.centroid();
      const Real farDistance = magnitude(d);
      if (!(farDistance > 0.0)) continue;
      const Vector2 inward = d * (1.0 / farDistance);

      const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, opposite);
      if (!crossing.has_value()) continue;
      const auto intersection = MeshGeometry::boundaryLineIntersection(mesh, bf, inward);
      if (!intersection.valid) continue;

      const Vector2 referenceNormal = MeshGeometry::unitNormal(bf);
      const Vector2 oppositeNormal = (opposite.owner() == cell.id())
                                         ? MeshGeometry::unitNormal(opposite)
                                         : (MeshGeometry::unitNormal(opposite) * -1.0);
      const Real alignment = dot(referenceNormal, oppositeNormal);

      // Same deterministic resolution as the CPU sweep.
      auto existing = perCell.end();
      for (auto it = perCell.begin(); it != perCell.end(); ++it) {
        if (std::get<0>(*it) == *oppositeId) {
          existing = it;
          break;
        }
      }
      const Vector2 offset = intersection.point - bf.centroid();
      if (existing == perCell.end()) {
        const Index idx = static_cast<Index>(cCell.size());
        cCell.push_back(cellId);
        cTarget.push_back(*oppositeId);
        cBoundary.push_back(faceId);
        cFar.push_back(farCellId);
        cFarDist.push_back(farDistance);
        cBackDist.push_back(intersection.distance);
        cW.push_back(crossing->t);
        cOffX.push_back(offset.x);
        cOffY.push_back(offset.y);
        cOffZ.push_back(offset.z);
        perCell.emplace_back(*oppositeId, faceId, alignment, idx);
      } else if (std::make_pair(alignment, faceId) <
                 std::make_pair(std::get<2>(*existing), std::get<1>(*existing))) {
        const Index idx = std::get<3>(*existing);
        cBoundary[idx] = faceId;
        cFar[idx] = farCellId;
        cFarDist[idx] = farDistance;
        cBackDist[idx] = intersection.distance;
        cW[idx] = crossing->t;
        cOffX[idx] = offset.x;
        cOffY[idx] = offset.y;
        cOffZ[idx] = offset.z;
        *existing = std::make_tuple(*oppositeId, faceId, alignment, idx);
      }
    }
  }
  claimRowBegin[nc] = static_cast<Index>(cCell.size());
  claimCount_ = static_cast<Index>(cCell.size());

  // --- slot -> claim map ---------------------------------------------------
  // One entry per (cell, face) slot of the CSR connectivity, mirroring the
  // CPU's "is this face claimed for this cell" lookup without a search.
  std::vector<Index> slotClaim;
  {
    std::size_t slots = 0;
    for (Index c = 0; c < nc; ++c) slots += mesh.cell(c).faceIds().size();
    slotClaim.assign(slots, kNoClaim);
    std::size_t slot = 0;
    for (Index c = 0; c < nc; ++c) {
      for (const Index faceId : mesh.cell(c).faceIds()) {
        for (Index k = claimRowBegin[c]; k < claimRowBegin[c + 1]; ++k) {
          if (cTarget[k] == faceId) {
            slotClaim[slot] = k;
            break;
          }
        }
        ++slot;
      }
    }
  }

  // Exactly greenGaussGradient's own loop condition. Note that claims alone do
  // NOT trigger the sweeps: on an aligned Cartesian mesh every claim exists but
  // every transfer offset is zero, so the CPU runs a single sweep.
  boundaryTransfer_ = boundaryTransferNeededRestated(mesh);
  sweepsNeeded_ = !skewIds.empty() || !obliqueIds.empty() || boundaryTransfer_;

  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  };
  put(faceDPf_, dPf);
  put(faceDNf_, dNf);
  put(boundaryA_, bA);
  put(boundaryB_, bB);
  put(boundaryKind_, bKind);
  put(skewedFaceIds_, skewIds);
  put(skewVecX_, skewX);
  put(skewVecY_, skewY);
  put(skewVecZ_, skewZ);
  put(skewTOfFace_, skewT);
  put(obliqueFaceIds_, obliqueIds);
  put(obliqueA_, oblA);
  put(obliqueB_, oblB);
  put(obliqueKind_, oblKind);
  put(obliqueTangentX_, oblTx);
  put(obliqueTangentY_, oblTy);
  put(obliqueTangentZ_, oblTz);
  put(claimCell_, cCell);
  put(claimTargetFace_, cTarget);
  put(claimBoundaryFace_, cBoundary);
  put(claimFarCell_, cFar);
  put(claimFarDistance_, cFarDist);
  put(claimBackDistance_, cBackDist);
  put(claimW_, cW);
  put(claimOffsetX_, cOffX);
  put(claimOffsetY_, cOffY);
  put(claimOffsetZ_, cOffZ);
  put(slotClaim_, slotClaim);

  usable_ = true;
  return true;
}

std::size_t DeviceGradientPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return mesh_.residentBytes() + bytes(faceDPf_) + bytes(faceDNf_) + bytes(boundaryA_) +
         bytes(boundaryB_) + bytes(boundaryKind_) + bytes(skewedFaceIds_) + bytes(skewVecX_) + bytes(skewVecY_) +
         bytes(skewVecZ_) + bytes(skewTOfFace_) + bytes(obliqueFaceIds_) + bytes(obliqueA_) +
         bytes(obliqueB_) + bytes(obliqueKind_) + bytes(obliqueTangentX_) + bytes(obliqueTangentY_) +
         bytes(obliqueTangentZ_) + bytes(claimCell_) + bytes(claimTargetFace_) +
         bytes(claimBoundaryFace_) + bytes(claimFarCell_) + bytes(claimFarDistance_) +
         bytes(claimBackDistance_) + bytes(claimW_) + bytes(claimOffsetX_) + bytes(claimOffsetY_) +
         bytes(claimOffsetZ_) + bytes(slotClaim_) + bytes(faceValues_) + bytes(claimValues_);
}

}  // namespace cfd::gpu
