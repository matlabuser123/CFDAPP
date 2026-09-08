#include "cfd/mesh/Face.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::mesh {

namespace {

void validate(const Vector2& centroid, const Vector2& areaVector) {
  if (!std::isfinite(centroid.x) || !std::isfinite(centroid.y)) {
    throw InvalidArgumentError("Face centroid must be finite");
  }
  if (!std::isfinite(areaVector.x) || !std::isfinite(areaVector.y)) {
    throw InvalidArgumentError("Face area vector must be finite");
  }
  if (!(magnitude(areaVector) > 0.0)) {
    throw InvalidArgumentError("Face area must be positive");
  }
}

}  // namespace

Face::Face(Index id, Index owner, std::optional<Index> neighbor, Vector2 centroid,
           Vector2 areaVector)
    : id_(id), owner_(owner), neighbor_(neighbor), centroid_(centroid), areaVector_(areaVector) {
  validate(centroid_, areaVector_);
  if (neighbor_.has_value() && *neighbor_ == owner_) {
    throw InvalidArgumentError("Face owner and neighbor must differ");
  }
}

Index Face::id() const noexcept { return id_; }
Index Face::owner() const noexcept { return owner_; }
std::optional<Index> Face::neighbor() const noexcept { return neighbor_; }
const Vector2& Face::centroid() const noexcept { return centroid_; }
const Vector2& Face::areaVector() const noexcept { return areaVector_; }
Real Face::area() const noexcept { return magnitude(areaVector_); }
bool Face::isBoundary() const noexcept { return !neighbor_.has_value(); }

}  // namespace cfd::mesh
