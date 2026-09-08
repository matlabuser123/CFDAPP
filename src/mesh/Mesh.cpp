#include "cfd/mesh/Mesh.hpp"

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

const BoundaryPatch& Mesh::boundaryPatch(std::string_view name) const {
  for (const auto& patch : boundaryPatches_) {
    if (patch.name() == name) {
      return patch;
    }
  }
  throw InvalidArgumentError("No boundary patch named '" + std::string(name) + "'");
}

}  // namespace cfd::mesh
