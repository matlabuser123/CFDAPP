#include "cfd/mesh/BoundaryPatch.hpp"

#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::mesh {

BoundaryPatch::BoundaryPatch(std::string name, std::vector<Index> faceIds)
    : name_(std::move(name)), faceIds_(std::move(faceIds)) {
  if (name_.empty()) {
    throw InvalidArgumentError("Boundary patch name must not be empty");
  }
}

const std::string& BoundaryPatch::name() const noexcept { return name_; }
const std::vector<Index>& BoundaryPatch::faceIds() const noexcept { return faceIds_; }
std::size_t BoundaryPatch::size() const noexcept { return faceIds_.size(); }

}  // namespace cfd::mesh
