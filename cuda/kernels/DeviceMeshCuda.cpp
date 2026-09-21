// GPU-DISC-001A -- DeviceMesh::upload, the one place immutable mesh data
// crosses to the device.
//
// Deliberately plain host code: it flattens the host Mesh's AoS structures into
// the structure-of-arrays layout DeviceMesh documents, then performs one upload
// per attribute. No kernel runs here, and nothing is recomputed on the device
// that the host already knows -- face area magnitude is the single derived
// quantity, precomputed here because every flux kernel needs it and it is
// immutable.

#include "cfd/gpu/DeviceMesh.hpp"

#include <cmath>
#include <vector>

#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::gpu {

void DeviceMesh::upload(const cfd::mesh::Mesh& mesh) {
  const auto& cells = mesh.cells();
  const auto& faces = mesh.faces();
  cellCount_ = static_cast<cfd::Index>(cells.size());
  faceCount_ = static_cast<cfd::Index>(faces.size());
  dimension_ = mesh.dimension();

  // --- per-cell ------------------------------------------------------------
  std::vector<cfd::Real> volume(cells.size());
  std::vector<cfd::Real> cx(cells.size()), cy(cells.size()), cz(cells.size());
  // CSR connectivity: offsets has cellCount + 1 entries, the last being the
  // total face-reference count (the same convention DeviceCsrMatrix uses).
  std::vector<cfd::Index> faceOffsets(cells.size() + 1, 0);
  std::size_t totalFaceRefs = 0;
  for (std::size_t c = 0; c < cells.size(); ++c) {
    const auto& cell = cells[c];
    volume[c] = cell.volume();
    cx[c] = cell.centroid().x;
    cy[c] = cell.centroid().y;
    cz[c] = cell.centroid().z;
    faceOffsets[c] = static_cast<cfd::Index>(totalFaceRefs);
    totalFaceRefs += cell.faceIds().size();
  }
  faceOffsets[cells.size()] = static_cast<cfd::Index>(totalFaceRefs);

  std::vector<cfd::Index> cellFaces;
  cellFaces.reserve(totalFaceRefs);
  for (const auto& cell : cells) {
    for (const cfd::Index f : cell.faceIds()) cellFaces.push_back(f);
  }

  // --- per-face ------------------------------------------------------------
  std::vector<cfd::Index> owner(faces.size()), neighbor(faces.size());
  std::vector<cfd::Real> fcx(faces.size()), fcy(faces.size()), fcz(faces.size());
  std::vector<cfd::Real> sx(faces.size()), sy(faces.size()), sz(faces.size());
  std::vector<cfd::Real> area(faces.size());
  for (std::size_t f = 0; f < faces.size(); ++f) {
    const auto& face = faces[f];
    owner[f] = face.owner();
    neighbor[f] = face.neighbor().has_value() ? *face.neighbor() : kNoNeighbor;
    fcx[f] = face.centroid().x;
    fcy[f] = face.centroid().y;
    fcz[f] = face.centroid().z;
    const auto& s = face.areaVector();
    sx[f] = s.x;
    sy[f] = s.y;
    sz[f] = s.z;
    // Taken from the host Face rather than recomputed from the components, so
    // the device sees exactly the magnitude the CPU operators use.
    area[f] = face.area();
  }

  // --- boundary patches ----------------------------------------------------
  // One flat face-id array; each patch is a contiguous slice of it. Patch order
  // follows the mesh's own, so a caller can map a host BoundaryConditionSet
  // entry to its device slice by name.
  std::vector<cfd::Index> boundaryFaces;
  patches_.clear();
  for (const auto& patch : mesh.boundaryPatches()) {
    DeviceBoundaryPatch dp;
    dp.firstFace = static_cast<cfd::Index>(boundaryFaces.size());
    dp.faceCount = static_cast<cfd::Index>(patch.faceIds().size());
    dp.name = patch.name();
    for (const cfd::Index f : patch.faceIds()) boundaryFaces.push_back(f);
    patches_.push_back(std::move(dp));
  }

  // --- upload --------------------------------------------------------------
  auto put = [](auto& buffer, const auto& host) {
    buffer.resize(static_cast<cfd::Index>(host.size()));
    if (!host.empty()) buffer.uploadFrom(host.data(), static_cast<cfd::Index>(host.size()));
  };

  put(cellVolumes_, volume);
  put(cellCentroidX_, cx);
  put(cellCentroidY_, cy);
  put(cellCentroidZ_, cz);
  put(faceOwner_, owner);
  put(faceNeighbor_, neighbor);
  put(faceCentroidX_, fcx);
  put(faceCentroidY_, fcy);
  put(faceCentroidZ_, fcz);
  put(faceAreaX_, sx);
  put(faceAreaY_, sy);
  put(faceAreaZ_, sz);
  put(faceArea_, area);
  put(cellFaceOffsets_, faceOffsets);
  put(cellFaceIds_, cellFaces);
  put(boundaryFaceIds_, boundaryFaces);

  uploaded_ = true;
}

std::size_t DeviceMesh::residentBytes() const noexcept {
  // DeviceBuffer exposes no value_type, so the element size is taken from its
  // data pointer in an unevaluated context -- correct even when the buffer is
  // empty and data() is null.
  const auto bytes = [](const auto& buffer) {
    return static_cast<std::size_t>(buffer.size()) * sizeof(*buffer.data());
  };
  return bytes(cellVolumes_) + bytes(cellCentroidX_) + bytes(cellCentroidY_) +
         bytes(cellCentroidZ_) + bytes(faceOwner_) + bytes(faceNeighbor_) + bytes(faceCentroidX_) +
         bytes(faceCentroidY_) + bytes(faceCentroidZ_) + bytes(faceAreaX_) + bytes(faceAreaY_) +
         bytes(faceAreaZ_) + bytes(faceArea_) + bytes(cellFaceOffsets_) + bytes(cellFaceIds_) +
         bytes(boundaryFaceIds_);
}

}  // namespace cfd::gpu
