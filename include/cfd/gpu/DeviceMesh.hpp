#pragma once

// GPU-DISC-001A -- CUDA-only. See DeviceBuffer.hpp's own header comment for the
// "never include from a CPU-only-compiled file" rule this file inherits.
//
// The immutable half of the mesh, mirrored once into persistent device memory.
//
// Everything here is fixed for the lifetime of a solve on a static mesh:
// connectivity, centroids, volumes, area vectors, boundary mapping. It is
// separated from the mutable solution fields deliberately -- GPU-PIPE-001
// Phase 1 measured that re-uploading immutable data per iteration is exactly
// the pattern that makes a GPU path slower than the CPU, and Phase 4 confirmed
// the linear-algebra side already avoids it. This type extends the same
// discipline to geometry.
//
// Layout is structure-of-arrays, one flat DeviceBuffer per attribute, indexed
// by cell id or face id. Vectors are stored as three separate coordinate arrays
// rather than an array of structs: every kernel that reads a centroid or an
// area vector reads all three components for a contiguous range of ids, so SoA
// gives coalesced access, and cfd::Vector2 is an alias for Vector3 (24 bytes)
// so an AoS layout would also straddle cache lines awkwardly.
//
// Connectivity is CSR, the same shape DeviceCsrMatrix already uses: a cell's
// faces are cellFaceIds[cellFaceOffsets[c] .. cellFaceOffsets[c+1]).

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"

namespace cfd::mesh {
class Mesh;
}

namespace cfd::gpu {

// A boundary patch as the device sees it: a contiguous slice of a face-id list,
// plus the patch's identity so a kernel can be given the right coefficients.
struct DeviceBoundaryPatch {
  cfd::Index firstFace{};  // offset into DeviceMesh::boundaryFaceIds()
  cfd::Index faceCount{};
  std::string name;  // host-side only; kernels receive the slice, not the name
};

class DeviceMesh {
 public:
  DeviceMesh() = default;

  // Uploads `mesh`'s immutable geometry and connectivity. Safe to call again:
  // buffers are reused when the sizes are unchanged (DeviceBuffer's never-shrink
  // contract), so re-uploading the same mesh performs no allocation. A mesh with
  // different dimensions reallocates, which is visible in
  // GPUExecutionStats::reallocations.
  void upload(const cfd::mesh::Mesh& mesh);

  [[nodiscard]] bool uploaded() const noexcept { return uploaded_; }
  [[nodiscard]] cfd::Index cellCount() const noexcept { return cellCount_; }
  [[nodiscard]] cfd::Index faceCount() const noexcept { return faceCount_; }
  [[nodiscard]] int dimension() const noexcept { return dimension_; }

  // --- per-cell, indexed by cell id ---------------------------------------
  [[nodiscard]] const cfd::Real* cellVolumes() const noexcept { return cellVolumes_.data(); }
  [[nodiscard]] const cfd::Real* cellCentroidX() const noexcept { return cellCentroidX_.data(); }
  [[nodiscard]] const cfd::Real* cellCentroidY() const noexcept { return cellCentroidY_.data(); }
  [[nodiscard]] const cfd::Real* cellCentroidZ() const noexcept { return cellCentroidZ_.data(); }

  // --- per-face, indexed by face id ---------------------------------------
  [[nodiscard]] const cfd::Index* faceOwner() const noexcept { return faceOwner_.data(); }
  // Neighbour id, or kNoNeighbor for a boundary face. Stored rather than an
  // optional so kernels can branch on a sentinel instead of a side table.
  [[nodiscard]] const cfd::Index* faceNeighbor() const noexcept { return faceNeighbor_.data(); }
  [[nodiscard]] const cfd::Real* faceCentroidX() const noexcept { return faceCentroidX_.data(); }
  [[nodiscard]] const cfd::Real* faceCentroidY() const noexcept { return faceCentroidY_.data(); }
  [[nodiscard]] const cfd::Real* faceCentroidZ() const noexcept { return faceCentroidZ_.data(); }
  [[nodiscard]] const cfd::Real* faceAreaX() const noexcept { return faceAreaX_.data(); }
  [[nodiscard]] const cfd::Real* faceAreaY() const noexcept { return faceAreaY_.data(); }
  [[nodiscard]] const cfd::Real* faceAreaZ() const noexcept { return faceAreaZ_.data(); }
  // |areaVector|, precomputed because every flux kernel needs it and a sqrt per
  // face per kernel is pure repeated work on immutable data.
  [[nodiscard]] const cfd::Real* faceArea() const noexcept { return faceArea_.data(); }

  // --- connectivity (CSR over cells) --------------------------------------
  [[nodiscard]] const cfd::Index* cellFaceOffsets() const noexcept {
    return cellFaceOffsets_.data();
  }
  [[nodiscard]] const cfd::Index* cellFaceIds() const noexcept { return cellFaceIds_.data(); }

  // --- boundary faces ------------------------------------------------------
  [[nodiscard]] const cfd::Index* boundaryFaceIds() const noexcept {
    return boundaryFaceIds_.data();
  }
  // GPU-DISC-001B: how many ids that pointer addresses. Additive accessor; the
  // buffer and its contents are unchanged from 001A's qualification.
  [[nodiscard]] cfd::Index boundaryFaceCount() const noexcept { return boundaryFaceIds_.size(); }
  [[nodiscard]] const std::vector<DeviceBoundaryPatch>& patches() const noexcept {
    return patches_;
  }

  // Sentinel for "this face has no neighbour", i.e. it is a boundary face.
  // Index is unsigned (see cfd/core/Types.hpp), so the largest value is the
  // natural choice and can never collide with a real cell id.
  static constexpr cfd::Index kNoNeighbor = static_cast<cfd::Index>(-1);

  // Total bytes resident on the device, for the memory report GPU-DISC-001
  // requires. Computed from the buffers themselves, not estimated.
  [[nodiscard]] std::size_t residentBytes() const noexcept;

 private:
  bool uploaded_{false};
  cfd::Index cellCount_{};
  cfd::Index faceCount_{};
  int dimension_{};

  DeviceBuffer<cfd::Real> cellVolumes_, cellCentroidX_, cellCentroidY_, cellCentroidZ_;
  DeviceBuffer<cfd::Index> faceOwner_, faceNeighbor_;
  DeviceBuffer<cfd::Real> faceCentroidX_, faceCentroidY_, faceCentroidZ_;
  DeviceBuffer<cfd::Real> faceAreaX_, faceAreaY_, faceAreaZ_, faceArea_;
  DeviceBuffer<cfd::Index> cellFaceOffsets_, cellFaceIds_;
  DeviceBuffer<cfd::Index> boundaryFaceIds_;
  std::vector<DeviceBoundaryPatch> patches_;
};

}  // namespace cfd::gpu
