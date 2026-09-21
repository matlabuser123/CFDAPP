// GPU-DISC-001I -- host-side construction of the pressure-correction plan.
//
// Two decisions are made once here, both exact and both easy to get wrong:
//
//   pinReferenceCell = !hasOpenBoundary
//       A FixedValue pressure patch ANYWHERE removes the null space
//       physically, so pinning as well would over-constrain an already
//       well-posed system. The device must reproduce the DECISION.
//
//   isAxisAligned(sf) && exactlyParallel(d, sf)
//       Exact predicates, evaluated with the d that MATCHES the face kind --
//       faceCentroid - ownerCentroid on a boundary face, neighbour - owner on
//       an internal one. The face-flux plan's flags use only the internal
//       form, so they are rebuilt here rather than reused.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/gpu/DevicePressureCorrection.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
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

bool isAxisAligned(const Vector2& sf) noexcept {
  const int nonZero = (sf.x != 0.0 ? 1 : 0) + (sf.y != 0.0 ? 1 : 0) + (sf.z != 0.0 ? 1 : 0);
  return nonZero == 1;
}
bool exactlyParallel(const Vector2& a, const Vector2& b) noexcept {
  return cross(a, b) == Vector3{};
}

// makeGradientBoundaries (PressureCorrectionEquation.cpp:43): the p' boundary
// conditions the explicit term's gradient uses.
cfd::boundary::BoundaryConditionSet makeGradientBoundaries(
    const Mesh& mesh, const cfd::boundary::BoundaryConditionSet& pressureBoundaries) {
  cfd::boundary::BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() ==
        cfd::boundary::BoundaryConditionType::FixedValue) {
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    } else {
      boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }
  return boundaries;
}

}  // namespace

bool DevicePressureCorrectionPlan::build(
    const Mesh& mesh, const cfd::boundary::BoundaryConditionSet& pressureBoundaries) {
  usable_ = false;
  unsupportedReason_.clear();
  fixedValueFaces_ = axisAlignedFaces_ = generalFaces_ = 0;
  mesh_.upload(mesh);
  threeDimensional_ = mesh.dimension() == 3;

  const Index nc = static_cast<Index>(mesh.numberOfCells());
  const Index nf = static_cast<Index>(mesh.numberOfFaces());
  cellCount_ = nc;
  faceCount_ = nf;

  // hasOpenBoundary: any patch whose pressure condition is FixedValue.
  bool hasOpenBoundary = false;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() ==
        cfd::boundary::BoundaryConditionType::FixedValue) {
      hasOpenBoundary = true;
      break;
    }
  }
  pinReferenceCell_ = !hasOpenBoundary;

  if (!previousGradient_.build(mesh, makeGradientBoundaries(mesh, pressureBoundaries))) {
    unsupportedReason_ = "p' gradient plan unusable: " + previousGradient_.unsupportedReason();
    return false;
  }

  std::vector<Real> dx(nf, 0.0), dy(nf, 0.0), dz(nf, 0.0), distance(nf, 0.0);
  std::vector<Real> dPf(nf, 0.0), dNf(nf, 0.0);
  std::vector<Index> aligned(nf, 0), axis(nf, 0), zeroZ(nf, 0), fixedValue(nf, 0);

  for (Index f = 0; f < nf; ++f) {
    const Face& face = mesh.face(f);
    const Vector2& sf = face.areaVector();
    zeroZ[f] = (sf.z == 0.0) ? 1 : 0;
    const Index ownerId = face.owner();
    Vector2 d{0.0, 0.0, 0.0};
    if (face.isBoundary()) {
      d = face.centroid() - mesh.cell(ownerId).centroid();
      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), pressureBoundaries);
      if (dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc) == nullptr) {
        unsupportedReason_ =
            "face " + std::to_string(f) + ": pressure condition is not scalar-valued";
        return false;
      }
      if (bc.type() == cfd::boundary::BoundaryConditionType::FixedValue) {
        fixedValue[f] = 1;
        ++fixedValueFaces_;
      }
    } else {
      const Index neighborId = *face.neighbor();
      d = mesh.cell(neighborId).centroid() - mesh.cell(ownerId).centroid();
      dPf[f] = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      dNf[f] = MeshGeometry::distance(mesh.cell(neighborId).centroid(), face.centroid());
    }
    dx[f] = d.x;
    dy[f] = d.y;
    dz[f] = d.z;
    distance[f] = magnitude(d);
    if (isAxisAligned(sf) && exactlyParallel(d, sf)) {
      aligned[f] = 1;
      axis[f] = (sf.x != 0.0) ? 0 : ((sf.y != 0.0) ? 1 : 2);
      ++axisAlignedFaces_;
    } else {
      ++generalFaces_;
    }
  }

  // Gather structures: cell.faceIds() order for the continuity imbalance, and
  // face-id order for the matrix (SparseMatrixBuilder's stable sort).
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
  // The pinned row's off-diagonals are removed per referenceCell by
  // ensureStructure(); the full pattern is kept so that derivation needs no
  // Mesh. rowOffsets_/columnIndices_ are uploaded there, not here.
  fullRowOffsets_ = rowOffsets;
  fullColumnIndices_ = columnIndices;
  structureReferenceCell_ = kNoStructure;
  entryCount_ = static_cast<Index>(columnIndices.size());

  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  };
  put(dX_, dx); put(dY_, dy); put(dZ_, dz); put(distance_, distance);
  put(faceDPf_, dPf); put(faceDNf_, dNf);
  put(axisAligned_, aligned); put(axisIndex_, axis); put(areaZIsZero_, zeroZ);
  put(isFixedValue_, fixedValue);
  put(cellFaceOffsets_, faceOffsets); put(cellFaceSorted_, faceSorted);

  usable_ = true;
  return true;
}

void DevicePressureCorrectionPlan::ensureStructure(Index referenceCell) const {
  if (structureReferenceCell_ == referenceCell) return;

  const Index nc = cellCount_;
  std::vector<Index> rowOffsets, columnIndices;
  if (!pinReferenceCell_ || nc == 0) {
    // Nothing is pinned, so no row is all-zeros-but-the-diagonal by
    // construction and the full pattern already matches what the host solver
    // receives. Still keyed, so the upload happens exactly once.
    rowOffsets = fullRowOffsets_;
    columnIndices = fullColumnIndices_;
  } else {
    rowOffsets.resize(static_cast<std::size_t>(nc) + 1);
    columnIndices.reserve(fullColumnIndices_.size());
    for (Index c = 0; c < nc; ++c) {
      rowOffsets[c] = static_cast<Index>(columnIndices.size());
      for (Index k = fullRowOffsets_[c]; k < fullRowOffsets_[c + 1]; ++k) {
        // The pinned row keeps ONLY its diagonal: assembleKernel writes 1.0
        // there and exactly 0.0 at every other column, and toHostSystem()
        // drops exactly those zeros. Every other row is untouched.
        if (c == referenceCell && fullColumnIndices_[k] != c) continue;
        columnIndices.push_back(fullColumnIndices_[k]);
      }
    }
    rowOffsets[nc] = static_cast<Index>(columnIndices.size());
  }

  entryCount_ = static_cast<Index>(columnIndices.size());
  rowOffsets_.resize(static_cast<Index>(rowOffsets.size()));
  if (!rowOffsets.empty())
    rowOffsets_.uploadFrom(rowOffsets.data(), static_cast<Index>(rowOffsets.size()));
  columnIndices_.resize(entryCount_);
  if (entryCount_ > 0) columnIndices_.uploadFrom(columnIndices.data(), entryCount_);
  structureReferenceCell_ = referenceCell;
}

std::size_t DevicePressureCorrectionPlan::residentBytes() const noexcept {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  return mesh_.residentBytes() + previousGradient_.residentBytes() + bytes(dX_) + bytes(dY_) +
         bytes(dZ_) + bytes(distance_) + bytes(faceDPf_) + bytes(faceDNf_) + bytes(axisAligned_) +
         bytes(axisIndex_) + bytes(areaZIsZero_) + bytes(isFixedValue_) + bytes(cellFaceOffsets_) +
         bytes(cellFaceSorted_) + bytes(rowOffsets_) + bytes(columnIndices_);
}

}  // namespace cfd::gpu
