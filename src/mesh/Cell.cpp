#include "cfd/mesh/Cell.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::mesh {

namespace {

void validate(const Vector2& centroid, Real volume) {
  if (!std::isfinite(centroid.x) || !std::isfinite(centroid.y)) {
    throw InvalidArgumentError("Cell centroid must be finite");
  }
  if (!std::isfinite(volume)) {
    throw InvalidArgumentError("Cell volume must be finite");
  }
  if (!(volume > 0.0)) {
    throw InvalidArgumentError("Cell volume must be positive");
  }
}

}  // namespace

Cell::Cell(Id id, Vector2 centroid, Real volume) : id_(id), centroid_(centroid), volume_(volume) {
  validate(centroid_, volume_);
}

Cell::Id Cell::id() const noexcept { return id_; }
const Vector2& Cell::centroid() const noexcept { return centroid_; }
Real Cell::volume() const noexcept { return volume_; }
const std::vector<Index>& Cell::faceIds() const noexcept { return faceIds_; }

void Cell::addFace(Index faceId) { faceIds_.push_back(faceId); }

}  // namespace cfd::mesh
