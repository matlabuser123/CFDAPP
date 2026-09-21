// GPU-DISC-001J -- host-side construction of the least-squares gradient plan.
//
// Plain host code that calls the SAME MeshGeometry and boundary functions the
// CPU gradient calls, and restates Gradient.cpp's normal-equation arithmetic
// expression for expression. Nothing here re-derives geometry with its own
// formula.
//
// The one restated predicate is obliqueNeumannFace, which Gradient.cpp keeps
// file-static; it lives in ObliqueNeumann.hpp, shared with the Green-Gauss
// plan, so there is exactly one copy.

#include <cmath>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/gpu/BoundaryEncoding.hpp"
#include "cfd/gpu/DeviceLeastSquaresGradient.hpp"
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

template <typename T>
void put(DeviceBuffer<T>& buffer, const std::vector<T>& host) {
  if (host.empty()) {
    buffer.resize(0);
    return;
  }
  buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
}

}  // namespace

bool DeviceLeastSquaresGradientPlan::build(const Mesh& mesh,
                                           const cfd::boundary::BoundaryConditionSet& boundaries) {
  usable_ = false;
  unsupportedReason_.clear();
  obliqueEntries_ = 0;
  skippedEntries_ = 0;
  illConditioned_ = 0;
  threeDimensional_ = 0;

  // The fallback path is the production Green-Gauss gradient with the same
  // boundary set; if it cannot be reproduced bitwise, neither can this.
  if (!greenGauss_.build(mesh, boundaries)) {
    unsupportedReason_ = "green-gauss fallback plan: " + greenGauss_.unsupportedReason();
    return false;
  }

  cellCount_ = mesh.numberOfCells();

  std::vector<Index> offsets, kinds, neighbors, encodings, threeD, conditioned;
  std::vector<Real> aValues, bValues, wdx, wdy, wdz;
  std::vector<Real> c11, c12, c13, c22, c23, c33, determinants;
  offsets.reserve(static_cast<std::size_t>(cellCount_) + 1);
  threeD.reserve(static_cast<std::size_t>(cellCount_));
  conditioned.reserve(static_cast<std::size_t>(cellCount_));

  offsets.push_back(0);
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Vector2 cellCentroid = cell.centroid();

    // Pass 1 -- the displacements, exactly as leastSquaresGradient gathers
    // them, in cell.faceIds() order. Also the per-entry boundary encoding,
    // which needs the distance that entry will use.
    struct Entry {
      Index kind;
      Index neighbor;
      Real a, b;
      Index encoding;
      Vector3 displacement;
    };
    std::vector<Entry> entries;
    entries.reserve(cell.faceIds().size());

    for (const Index faceId : cell.faceIds()) {
      const Face& face = mesh.face(faceId);
      if (!face.isBoundary()) {
        const Index neighborId = (face.owner() == id) ? *face.neighbor() : face.owner();
        entries.push_back({kLsEntryInterior, neighborId, 0.0, 0.0, kBoundaryEncodingConstant,
                           mesh.cell(neighborId).centroid() - cellCentroid});
        continue;
      }
      const cfd::boundary::BoundaryCondition& bc =
          cfd::boundary::boundaryConditionForFace(mesh, faceId, boundaries);
      const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        unsupportedReason_ = "face " + std::to_string(faceId) + ": not scalar-valued";
        return false;
      }
      const ObliqueFace oblique = obliqueNeumannFaceRestated(mesh, face, boundaries);
      const Real distance = oblique.applies
                                ? oblique.normalDistance
                                : MeshGeometry::distance(cellCentroid, face.centroid());
      Real a = 0.0, b = 0.0;
      Index encoding = kBoundaryEncodingConstant;
      if (!encodeBoundaryCondition(*scalarBc, distance, a, b, encoding)) {
        unsupportedReason_ = "face " + std::to_string(faceId) +
                             ": boundary condition is not reproducible bitwise by the "
                             "constant/shift/affine encoding";
        return false;
      }
      const Vector3 displacement = oblique.applies
                                       ? (oblique.unitNormal * oblique.normalDistance)
                                       : (face.centroid() - cellCentroid);
      if (oblique.applies) ++obliqueEntries_;
      entries.push_back({kLsEntryBoundary, DeviceMesh::kNoNeighbor, a, b, encoding, displacement});
    }

    // Per-cell 2D/3D verdict: ANY displacement with a non-zero z, exactly the
    // CPU's std::any_of. Not a mesh-level property in the CPU code, so not one
    // here either.
    bool cellIsThreeD = false;
    for (const Entry& entry : entries) {
      if (entry.displacement.z != 0.0) {
        cellIsThreeD = true;
        break;
      }
    }
    if (cellIsThreeD) ++threeDimensional_;

    // Pass 2 -- the normal matrix, accumulated in the same order with the same
    // zero-distance skip and the same operand grouping:
    //   Sxx += weight * dx * dx   is   ((weight * dx) * dx)
    // so (weight * dx) is formed once and reused, which is what the device
    // multiplies dphi by.
    Real sxx = 0.0, sxy = 0.0, sxz = 0.0, syy = 0.0, syz = 0.0, szz = 0.0;
    for (const Entry& entry : entries) {
      const Real dx = entry.displacement.x;
      const Real dy = entry.displacement.y;
      const Real dz = entry.displacement.z;
      const Real distanceSquared =
          cellIsThreeD ? ((dx * dx) + (dy * dy) + (dz * dz)) : ((dx * dx) + (dy * dy));
      if (!(distanceSquared > 0.0)) {
        kinds.push_back(kLsEntrySkipped);
        neighbors.push_back(DeviceMesh::kNoNeighbor);
        aValues.push_back(0.0);
        bValues.push_back(0.0);
        encodings.push_back(kBoundaryEncodingConstant);
        wdx.push_back(0.0);
        wdy.push_back(0.0);
        wdz.push_back(0.0);
        ++skippedEntries_;
        continue;
      }
      const Real weight = 1.0 / distanceSquared;
      const Real wx = weight * dx;
      const Real wy = weight * dy;
      const Real wz = weight * dz;
      sxx += wx * dx;
      sxy += wx * dy;
      syy += wy * dy;
      if (cellIsThreeD) {
        sxz += wx * dz;
        syz += wy * dz;
        szz += wz * dz;
      }
      kinds.push_back(entry.kind);
      neighbors.push_back(entry.neighbor);
      aValues.push_back(entry.a);
      bValues.push_back(entry.b);
      encodings.push_back(entry.encoding);
      wdx.push_back(wx);
      wdy.push_back(wy);
      wdz.push_back(wz);
    }
    offsets.push_back(static_cast<Index>(kinds.size()));

    // Pass 3 -- cofactors, determinant and the conditioning verdict. The 2D
    // path stores its three distinct entries in c11/c12/c22 so the kernel needs
    // one layout: c11 = Syy, c12 = Sxy, c22 = Sxx, matching the 2x2 solve
    //   gx = (bx*Syy - by*Sxy)/det        gy = (Sxx*by - Sxy*bx)/det
    // whose asymmetric operand order is preserved in the kernel.
    Real determinant = 0.0, scale = 0.0;
    bool ok = false;
    if (cellIsThreeD) {
      const Real k11 = (syy * szz) - (syz * syz);
      const Real k12 = (sxz * syz) - (sxy * szz);
      const Real k13 = (sxy * syz) - (sxz * syy);
      const Real k22 = (sxx * szz) - (sxz * sxz);
      const Real k23 = (sxy * sxz) - (sxx * syz);
      const Real k33 = (sxx * syy) - (sxy * sxy);
      determinant = (sxx * k11) + (sxy * k12) + (sxz * k13);
      scale = sxx + syy + szz;
      ok = (scale > 0.0) && !(determinant < (1e-10 * scale * scale * scale));
      c11.push_back(k11);
      c12.push_back(k12);
      c13.push_back(k13);
      c22.push_back(k22);
      c23.push_back(k23);
      c33.push_back(k33);
    } else {
      determinant = (sxx * syy) - (sxy * sxy);
      scale = sxx + syy;
      ok = (scale > 0.0) && !(determinant < (1e-10 * scale * scale));
      c11.push_back(syy);
      c12.push_back(sxy);
      c13.push_back(0.0);
      c22.push_back(sxx);
      c23.push_back(0.0);
      c33.push_back(0.0);
    }
    determinants.push_back(determinant);
    threeD.push_back(cellIsThreeD ? 1 : 0);
    conditioned.push_back(ok ? 1 : 0);
    if (!ok) ++illConditioned_;
  }

  entryCount_ = static_cast<Index>(kinds.size());

  put(entryOffsets_, offsets);
  put(entryKind_, kinds);
  put(entryNeighbor_, neighbors);
  put(entryA_, aValues);
  put(entryB_, bValues);
  put(entryEncoding_, encodings);
  put(entryWdX_, wdx);
  put(entryWdY_, wdy);
  put(entryWdZ_, wdz);
  put(cellThreeD_, threeD);
  put(cellConditioned_, conditioned);
  put(c11_, c11);
  put(c12_, c12);
  put(c13_, c13);
  put(c22_, c22);
  put(c23_, c23);
  put(c33_, c33);
  put(det_, determinants);

  usable_ = true;
  return true;
}

std::size_t DeviceLeastSquaresGradientPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return greenGauss_.residentBytes() + bytes(entryOffsets_) + bytes(entryKind_) +
         bytes(entryNeighbor_) + bytes(entryA_) + bytes(entryB_) + bytes(entryEncoding_) +
         bytes(entryWdX_) + bytes(entryWdY_) + bytes(entryWdZ_) + bytes(cellThreeD_) +
         bytes(cellConditioned_) + bytes(c11_) + bytes(c12_) + bytes(c13_) + bytes(c22_) +
         bytes(c23_) + bytes(c33_) + bytes(det_);
}

}  // namespace cfd::gpu
