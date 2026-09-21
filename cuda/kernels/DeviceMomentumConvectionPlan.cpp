// GPU-DISC-001D closure -- host-side construction of the momentum-convection plan.
//
// All geometry comes from the production MeshGeometry functions the CPU path
// calls: oppositeInteriorFace, ownerNeighborDistance, ownerNeighborCrossing,
// unitNormal, distance.
//
// See results/gpu-disc-001/convection/audit.md section 11.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"
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

struct FarUpstream {
  bool available = false;
  Index cellId = 0;
  Real hCU = 0.0;
};

FarUpstream farUpstreamFor(const Mesh& mesh, const Face& face, Index upwindCellId) {
  const auto farFaceId = MeshGeometry::oppositeInteriorFace(mesh, mesh.cell(upwindCellId), face);
  if (!farFaceId.has_value()) return {};
  const Face& farFace = mesh.face(*farFaceId);
  const Index farCellId =
      (farFace.owner() == upwindCellId) ? *farFace.neighbor() : farFace.owner();
  return {true, farCellId, MeshGeometry::ownerNeighborDistance(mesh, farFace)};
}

}  // namespace

bool DeviceMomentumConvectionPlan::build(
    const Mesh& mesh, const cfd::boundary::BoundaryConditionSet& velocityBoundaries) {
  usable_ = false;
  unsupportedReason_.clear();
  farOwnerUpwind_ = farNeighborUpwind_ = 0;
  constantFaces_ = identityFaces_ = symmetryFaces_ = 0;
  mesh_.upload(mesh);
  threeDimensional_ = mesh.dimension() == 3;

  const Index nc = static_cast<Index>(mesh.numberOfCells());
  const Index nf = static_cast<Index>(mesh.numberOfFaces());
  cellCount_ = nc;

  std::vector<Real> dPf(nf, 0.0), dNf(nf, 0.0);
  std::vector<Real> offOX(nf, 0.0), offOY(nf, 0.0), offOZ(nf, 0.0);
  std::vector<Real> offNX(nf, 0.0), offNY(nf, 0.0), offNZ(nf, 0.0);
  std::vector<Index> farOValid(nf, 0), farOCell(nf, 0), farNValid(nf, 0), farNCell(nf, 0);
  std::vector<Real> farOHCU(nf, 0.0), farNHCU(nf, 0.0);
  std::vector<Index> bKind(nf, kVectorBoundaryConstant);
  std::vector<Real> bCX(nf, 0.0), bCY(nf, 0.0), bCZ(nf, 0.0);
  std::vector<Real> bNX(nf, 0.0), bNY(nf, 0.0), bNZ(nf, 0.0);
  std::vector<Index> skewIds;
  std::vector<Real> skewT, skewX, skewY, skewZ;

  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance =
          MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      const Vector2 normal = MeshGeometry::unitNormal(face);
      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), velocityBoundaries);
      const auto* vectorBc = dynamic_cast<const cfd::boundary::VectorBoundaryCondition*>(&bc);
      if (vectorBc == nullptr) {
        unsupportedReason_ =
            "face " + std::to_string(f) + ": boundary condition is not vector-valued";
        return false;
      }
      Vector2 constantValue{0.0, 0.0, 0.0};
      if (!encodeVectorBoundaryCondition(*vectorBc, distance, normal, constantValue, bKind[f])) {
        unsupportedReason_ = "face " + std::to_string(f) + ": velocity boundary condition '" +
                             std::string(bc.name()) +
                             "' is not bitwise-reproducible as a constant, the identity or the "
                             "symmetry projection; mesh rejected for the device momentum "
                             "convection path";
        return false;
      }
      bCX[f] = constantValue.x;
      bCY[f] = constantValue.y;
      bCZ[f] = constantValue.z;
      bNX[f] = normal.x;
      bNY[f] = normal.y;
      bNZ[f] = normal.z;
      if (bKind[f] == kVectorBoundaryConstant) ++constantFaces_;
      else if (bKind[f] == kVectorBoundaryIdentity) ++identityFaces_;
      else ++symmetryFaces_;
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    dPf[f] = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
    dNf[f] = MeshGeometry::distance(mesh.cell(neighborId).centroid(), face.centroid());
    const Vector2 offsetOwner = face.centroid() - mesh.cell(ownerId).centroid();
    const Vector2 offsetNeighbor = face.centroid() - mesh.cell(neighborId).centroid();
    offOX[f] = offsetOwner.x; offOY[f] = offsetOwner.y; offOZ[f] = offsetOwner.z;
    offNX[f] = offsetNeighbor.x; offNY[f] = offsetNeighbor.y; offNZ[f] = offsetNeighbor.z;

    const FarUpstream farOwner = farUpstreamFor(mesh, face, ownerId);
    if (farOwner.available) {
      farOValid[f] = 1; farOCell[f] = farOwner.cellId; farOHCU[f] = farOwner.hCU;
      ++farOwnerUpwind_;
    }
    const FarUpstream farNeighbor = farUpstreamFor(mesh, face, neighborId);
    if (farNeighbor.available) {
      farNValid[f] = 1; farNCell[f] = farNeighbor.cellId; farNHCU[f] = farNeighbor.hCU;
      ++farNeighborUpwind_;
    }

    const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, face);
    if (crossing.has_value() && crossing->skewVector != Vector3{}) {
      skewIds.push_back(f);
      skewT.push_back(crossing->t);
      skewX.push_back(crossing->skewVector.x);
      skewY.push_back(crossing->skewVector.y);
      skewZ.push_back(crossing->skewVector.z);
    }
  }

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
  put(faceDPf_, dPf); put(faceDNf_, dNf);
  put(offsetOwnerX_, offOX); put(offsetOwnerY_, offOY); put(offsetOwnerZ_, offOZ);
  put(offsetNeighborX_, offNX); put(offsetNeighborY_, offNY); put(offsetNeighborZ_, offNZ);
  put(farOwnerValid_, farOValid); put(farOwnerCell_, farOCell); put(farOwnerHCU_, farOHCU);
  put(farNeighborValid_, farNValid); put(farNeighborCell_, farNCell); put(farNeighborHCU_, farNHCU);
  put(bKind_, bKind);
  put(bConstX_, bCX); put(bConstY_, bCY); put(bConstZ_, bCZ);
  put(bNormalX_, bNX); put(bNormalY_, bNY); put(bNormalZ_, bNZ);
  put(skewedFaceIds_, skewIds);
  put(skewT_, skewT); put(skewVecX_, skewX); put(skewVecY_, skewY); put(skewVecZ_, skewZ);
  put(cellFaceOffsets_, faceOffsets); put(cellFaceSorted_, faceSorted);
  put(rowOffsets_, rowOffsets); put(columnIndices_, columnIndices);

  usable_ = true;
  return true;
}

std::size_t DeviceMomentumConvectionPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return mesh_.residentBytes() + bytes(faceDPf_) + bytes(faceDNf_) + bytes(offsetOwnerX_) +
         bytes(offsetOwnerY_) + bytes(offsetOwnerZ_) + bytes(offsetNeighborX_) +
         bytes(offsetNeighborY_) + bytes(offsetNeighborZ_) + bytes(farOwnerValid_) +
         bytes(farOwnerCell_) + bytes(farOwnerHCU_) + bytes(farNeighborValid_) +
         bytes(farNeighborCell_) + bytes(farNeighborHCU_) + bytes(bKind_) + bytes(bConstX_) +
         bytes(bConstY_) + bytes(bConstZ_) + bytes(bNormalX_) + bytes(bNormalY_) +
         bytes(bNormalZ_) + bytes(skewedFaceIds_) + bytes(skewT_) + bytes(skewVecX_) +
         bytes(skewVecY_) + bytes(skewVecZ_) + bytes(cellFaceOffsets_) + bytes(cellFaceSorted_) +
         bytes(rowOffsets_) + bytes(columnIndices_);
}

}  // namespace cfd::gpu
