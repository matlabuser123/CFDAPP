// GPU-DISC-001D -- host-side construction of the immutable convection plan.
//
// Every geometric quantity comes from the production MeshGeometry functions the
// CPU convection path itself calls: oppositeInteriorFace, ownerNeighborDistance,
// distance. Nothing is re-derived locally.
//
// The one structural difference from the gradient and diffusion plans: the
// upwind side of a face is decided by the SIGN OF THE MASS FLUX, which is an
// input field, so every quantity that depends on "which side is upwind" -- the
// far-upstream cell, hCU, the face-centre offset -- is precomputed for BOTH
// orientations and selected in the kernel.
//
// See results/gpu-disc-001/convection/audit.md.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// Reproduces Convection.cpp's file-static findFarUpstreamCell for one
// orientation: the cell across the upwind cell's opposite interior face.
struct FarUpstream {
  bool available = false;
  Index cellId = 0;
  Real hCU = 0.0;
};

FarUpstream farUpstreamFor(const Mesh& mesh, const Face& face, Index upwindCellId) {
  const auto farFaceId =
      MeshGeometry::oppositeInteriorFace(mesh, mesh.cell(upwindCellId), face);
  if (!farFaceId.has_value()) return {};
  const Face& farFace = mesh.face(*farFaceId);
  const Index farCellId =
      (farFace.owner() == upwindCellId) ? *farFace.neighbor() : farFace.owner();
  return {true, farCellId, MeshGeometry::ownerNeighborDistance(mesh, farFace)};
}

}  // namespace

bool DeviceConvectionPlan::build(const Mesh& mesh,
                                 const cfd::boundary::BoundaryConditionSet& boundaries) {
  usable_ = false;
  unsupportedReason_.clear();
  farOwnerUpwind_ = 0;
  farNeighborUpwind_ = 0;

  // LinearUpwind needs a gradient, and the gradient plan's own boundary
  // verification is what rejects a mesh this path cannot reproduce bitwise.
  if (!gradient_.build(mesh, boundaries)) {
    unsupportedReason_ = "gradient plan unusable: " + gradient_.unsupportedReason();
    return false;
  }

  const Index nc = static_cast<Index>(mesh.numberOfCells());
  const Index nf = static_cast<Index>(mesh.numberOfFaces());
  cellCount_ = nc;

  std::vector<Real> dPf(nf, 0.0), dNf(nf, 0.0);
  std::vector<Real> offOX(nf, 0.0), offOY(nf, 0.0), offOZ(nf, 0.0);
  std::vector<Real> offNX(nf, 0.0), offNY(nf, 0.0), offNZ(nf, 0.0);
  std::vector<Index> farOValid(nf, 0), farOCell(nf, 0);
  std::vector<Real> farOHCU(nf, 0.0);
  std::vector<Index> farNValid(nf, 0), farNCell(nf, 0);
  std::vector<Real> farNHCU(nf, 0.0);
  std::vector<Real> bDistance(nf, 0.0), bA(nf, 0.0), bB(nf, 0.0);
  std::vector<Index> bKind(nf, kBoundaryEncodingConstant);

  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance =
          MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      bDistance[f] = distance;
      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
      const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        unsupportedReason_ =
            "face " + std::to_string(f) + ": boundary condition is not scalar-valued";
        return false;
      }
      if (!encodeBoundaryCondition(*scalarBc, distance, bA[f], bB[f], bKind[f])) {
        unsupportedReason_ = "face " + std::to_string(f) + ": boundary condition '" +
                             std::string(bc.name()) +
                             "' is not bitwise-reproducible as a constant, a shift or an affine "
                             "function of phi_P; mesh rejected for the device convection path";
        return false;
      }
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    dPf[f] = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
    dNf[f] = MeshGeometry::distance(mesh.cell(neighborId).centroid(), face.centroid());

    // linearUpwindFaceValue's Taylor offset: faceCentroid - upwindCentroid.
    const Vector2 offsetOwner = face.centroid() - mesh.cell(ownerId).centroid();
    const Vector2 offsetNeighbor = face.centroid() - mesh.cell(neighborId).centroid();
    offOX[f] = offsetOwner.x;
    offOY[f] = offsetOwner.y;
    offOZ[f] = offsetOwner.z;
    offNX[f] = offsetNeighbor.x;
    offNY[f] = offsetNeighbor.y;
    offNZ[f] = offsetNeighbor.z;

    const FarUpstream farOwner = farUpstreamFor(mesh, face, ownerId);
    if (farOwner.available) {
      farOValid[f] = 1;
      farOCell[f] = farOwner.cellId;
      farOHCU[f] = farOwner.hCU;
      ++farOwnerUpwind_;
    }
    const FarUpstream farNeighbor = farUpstreamFor(mesh, face, neighborId);
    if (farNeighbor.available) {
      farNValid[f] = 1;
      farNCell[f] = farNeighbor.cellId;
      farNHCU[f] = farNeighbor.hCU;
      ++farNeighborUpwind_;
    }
  }

  // Assembly gather structure -- same construction as the diffusion plan.
  std::vector<Index> faceOffsets(nc + 1, 0), faceSorted;
  std::vector<Index> rowOffsets(nc + 1, 0), columnIndices;
  {
    std::vector<Index> faces, columns;
    for (Index c = 0; c < nc; ++c) {
      faceOffsets[c] = static_cast<Index>(faceSorted.size());
      rowOffsets[c] = static_cast<Index>(columnIndices.size());
      faces.assign(mesh.cell(c).faceIds().begin(), mesh.cell(c).faceIds().end());
      std::sort(faces.begin(), faces.end());
      faceSorted.insert(faceSorted.end(), faces.begin(), faces.end());
      columns.clear();
      columns.push_back(c);
      for (const Index faceId : faces) {
        const Face& face = mesh.face(faceId);
        if (face.isBoundary()) continue;
        columns.push_back((face.owner() == c) ? *face.neighbor() : face.owner());
      }
      std::sort(columns.begin(), columns.end());
      columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
      columnIndices.insert(columnIndices.end(), columns.begin(), columns.end());
    }
    faceOffsets[nc] = static_cast<Index>(faceSorted.size());
    rowOffsets[nc] = static_cast<Index>(columnIndices.size());
  }
  entryCount_ = static_cast<Index>(columnIndices.size());

  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  };
  put(faceDPf_, dPf);
  put(faceDNf_, dNf);
  put(offsetOwnerX_, offOX);
  put(offsetOwnerY_, offOY);
  put(offsetOwnerZ_, offOZ);
  put(offsetNeighborX_, offNX);
  put(offsetNeighborY_, offNY);
  put(offsetNeighborZ_, offNZ);
  put(farOwnerValid_, farOValid);
  put(farOwnerCell_, farOCell);
  put(farOwnerHCU_, farOHCU);
  put(farNeighborValid_, farNValid);
  put(farNeighborCell_, farNCell);
  put(farNeighborHCU_, farNHCU);
  put(bDistance_, bDistance);
  put(bValueA_, bA);
  put(bValueB_, bB);
  put(bValueKind_, bKind);
  put(cellFaceOffsets_, faceOffsets);
  put(cellFaceSorted_, faceSorted);
  put(rowOffsets_, rowOffsets);
  put(columnIndices_, columnIndices);

  usable_ = true;
  return true;
}

std::size_t DeviceConvectionPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return gradient_.residentBytes() + bytes(faceDPf_) + bytes(faceDNf_) + bytes(offsetOwnerX_) +
         bytes(offsetOwnerY_) + bytes(offsetOwnerZ_) + bytes(offsetNeighborX_) +
         bytes(offsetNeighborY_) + bytes(offsetNeighborZ_) + bytes(farOwnerValid_) +
         bytes(farOwnerCell_) + bytes(farOwnerHCU_) + bytes(farNeighborValid_) +
         bytes(farNeighborCell_) + bytes(farNeighborHCU_) + bytes(bDistance_) + bytes(bValueA_) +
         bytes(bValueB_) + bytes(bValueKind_) + bytes(cellFaceOffsets_) + bytes(cellFaceSorted_) +
         bytes(rowOffsets_) + bytes(columnIndices_);
}

}  // namespace cfd::gpu
