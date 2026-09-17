#include "cfd/mesh/Mesh.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::mesh {

Mesh::Mesh(std::vector<Cell> cells, std::vector<Face> faces,
           std::vector<BoundaryPatch> boundaryPatches)
    : cells_(std::move(cells)),
      faces_(std::move(faces)),
      boundaryPatches_(std::move(boundaryPatches)) {
  if (cells_.empty()) {
    throw InvalidArgumentError("Mesh must contain at least one cell");
  }
  if (faces_.empty()) {
    throw InvalidArgumentError("Mesh must contain at least one face");
  }
  for (const auto& face : faces_) {
    if (face.areaVector().z != 0.0) {
      dimension_ = 3;
      break;
    }
  }
}

Mesh::Mesh(std::vector<Cell> cells, std::vector<Face> faces,
           std::vector<BoundaryPatch> boundaryPatches, StructuredGrid structuredGrid)
    : Mesh(std::move(cells), std::move(faces), std::move(boundaryPatches),
           std::vector<StructuredGrid>{std::move(structuredGrid)}) {}

Mesh::Mesh(std::vector<Cell> cells, std::vector<Face> faces,
           std::vector<BoundaryPatch> boundaryPatches, std::vector<StructuredGrid> structuredBlocks)
    : Mesh(std::move(cells), std::move(faces), std::move(boundaryPatches)) {
  std::size_t total = 0;
  for (const auto& grid : structuredBlocks) {
    if (grid.isThreeDimensional() != (dimension_ == 3)) {
      throw InvalidArgumentError(
          "Mesh: structured grid dimension (nz > 0 for 3D) does not match the mesh geometry");
    }
    if (grid.vertices.size() != grid.vertexCount()) {
      throw InvalidArgumentError(
          grid.isThreeDimensional()
              ? "Mesh: structured grid must have (nx + 1) * (ny + 1) * (nz + 1) vertices"
              : "Mesh: structured grid must have (nx + 1) * (ny + 1) vertices");
    }
    total += grid.cellCount();
  }
  if (total != cells_.size()) {
    throw InvalidArgumentError("Mesh: structured grid nx * ny does not match the cell count");
  }
  structuredBlocks_ = std::move(structuredBlocks);
}

const std::vector<Cell>& Mesh::cells() const noexcept { return cells_; }
const std::vector<Face>& Mesh::faces() const noexcept { return faces_; }
const std::vector<BoundaryPatch>& Mesh::boundaryPatches() const noexcept {
  return boundaryPatches_;
}

std::size_t Mesh::numberOfCells() const noexcept { return cells_.size(); }
std::size_t Mesh::numberOfFaces() const noexcept { return faces_.size(); }

const Cell& Mesh::cell(Index id) const {
  if (id >= cells_.size()) {
    throw InvalidArgumentError("Cell id out of range");
  }
  return cells_[id];
}

const Face& Mesh::face(Index id) const {
  if (id >= faces_.size()) {
    throw InvalidArgumentError("Face id out of range");
  }
  return faces_[id];
}

const StructuredGrid* Mesh::structuredGrid() const noexcept {
  return structuredBlocks_.size() == 1 ? &structuredBlocks_.front() : nullptr;
}

const std::vector<StructuredGrid>& Mesh::structuredBlocks() const noexcept {
  return structuredBlocks_;
}

int Mesh::dimension() const noexcept { return dimension_; }

MeshGeometryState Mesh::geometry() const {
  MeshGeometryState state;
  state.cellCentroids.reserve(cells_.size());
  state.cellVolumes.reserve(cells_.size());
  for (const auto& cell : cells_) {
    state.cellCentroids.push_back(cell.centroid());
    state.cellVolumes.push_back(cell.volume());
  }
  state.faceCentroids.reserve(faces_.size());
  state.faceAreaVectors.reserve(faces_.size());
  for (const auto& face : faces_) {
    state.faceCentroids.push_back(face.centroid());
    state.faceAreaVectors.push_back(face.areaVector());
  }
  state.blockVertices.reserve(structuredBlocks_.size());
  for (const auto& block : structuredBlocks_) state.blockVertices.push_back(block.vertices);
  return state;
}

void Mesh::setGeometry(const MeshGeometryState& geometry) {
  const auto fail = [](const std::string& message) {
    throw InvalidArgumentError("Mesh::setGeometry: " + message);
  };
  if (geometry.cellCentroids.size() != cells_.size() ||
      geometry.cellVolumes.size() != cells_.size()) {
    fail("expected " + std::to_string(cells_.size()) + " cell centroids and volumes");
  }
  if (geometry.faceCentroids.size() != faces_.size() ||
      geometry.faceAreaVectors.size() != faces_.size()) {
    fail("expected " + std::to_string(faces_.size()) + " face centroids and area vectors");
  }
  if (geometry.blockVertices.size() != structuredBlocks_.size()) {
    fail("expected vertices for " + std::to_string(structuredBlocks_.size()) + " structured blocks");
  }
  const bool planar = dimension_ == 2;
  for (std::size_t b = 0; b < structuredBlocks_.size(); ++b) {
    if (geometry.blockVertices[b].size() != structuredBlocks_[b].vertices.size()) {
      fail("block " + std::to_string(b) + " needs " +
           std::to_string(structuredBlocks_[b].vertices.size()) + " vertices");
    }
    for (std::size_t v = 0; v < geometry.blockVertices[b].size(); ++v) {
      const Vector3& p = geometry.blockVertices[b][v];
      if (!isFinite(p)) fail("block " + std::to_string(b) + " vertex " + std::to_string(v) +
                             " is not finite");
      if (planar && p.z != 0.0) {
        fail("block " + std::to_string(b) + " vertex " + std::to_string(v) +
             " leaves the xy-plane of a two-dimensional mesh");
      }
    }
  }
  for (std::size_t c = 0; c < cells_.size(); ++c) {
    if (!isFinite(geometry.cellCentroids[c]) || !std::isfinite(geometry.cellVolumes[c])) {
      fail("cell " + std::to_string(c) + " has a non-finite centroid or volume");
    }
    if (!(geometry.cellVolumes[c] > 0.0)) {
      fail("cell " + std::to_string(c) + " has a non-positive volume " +
           std::to_string(geometry.cellVolumes[c]));
    }
    if (planar && geometry.cellCentroids[c].z != 0.0) {
      fail("cell " + std::to_string(c) + " leaves the xy-plane of a two-dimensional mesh");
    }
  }
  for (std::size_t f = 0; f < faces_.size(); ++f) {
    if (!isFinite(geometry.faceCentroids[f]) || !isFinite(geometry.faceAreaVectors[f])) {
      fail("face " + std::to_string(f) + " has a non-finite centroid or area vector");
    }
    if (!(magnitude(geometry.faceAreaVectors[f]) > 0.0)) {
      fail("face " + std::to_string(f) + " has zero area");
    }
    if (planar && (geometry.faceCentroids[f].z != 0.0 || geometry.faceAreaVectors[f].z != 0.0)) {
      fail("face " + std::to_string(f) + " leaves the xy-plane of a two-dimensional mesh");
    }
  }
  for (std::size_t c = 0; c < cells_.size(); ++c) {
    cells_[c].setGeometry(geometry.cellCentroids[c], geometry.cellVolumes[c]);
  }
  for (std::size_t f = 0; f < faces_.size(); ++f) {
    faces_[f].setGeometry(geometry.faceCentroids[f], geometry.faceAreaVectors[f]);
  }
  for (std::size_t b = 0; b < structuredBlocks_.size(); ++b) {
    structuredBlocks_[b].vertices = geometry.blockVertices[b];
  }
}

void requireTwoDimensional(const Mesh& mesh, std::string_view component) {
  if (mesh.dimension() != 2) {
    throw InvalidArgumentError(std::string(component) +
                               " supports two-dimensional meshes only (this mesh is 3D; 3D "
                               "incompressible flow is P12-MESH-006, not implemented)");
  }
}

const BoundaryPatch& Mesh::boundaryPatch(std::string_view name) const {
  for (const auto& patch : boundaryPatches_) {
    if (patch.name() == name) {
      return patch;
    }
  }
  throw InvalidArgumentError("No boundary patch named '" + std::string(name) + "'");
}

}  // namespace cfd::mesh
