#pragma once

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::mesh {

class Mesh;

// One finite-volume control volume: identity and topology only. Mesh
// stores geometry; fields store solution values (pressure, velocity,
// temperature, residuals, equation coefficients, ...) -- never mix the
// two here.
class Cell {
 public:
  using Id = Index;

  Cell(Id id, Vector2 centroid, Real volume);

  [[nodiscard]] Id id() const noexcept;
  [[nodiscard]] const Vector2& centroid() const noexcept;
  [[nodiscard]] Real volume() const noexcept;

  [[nodiscard]] const std::vector<Index>& faceIds() const noexcept;

  void addFace(Index faceId);

 private:
  // P12-MESH-007: only Mesh::setGeometry (a topology-preserving geometry
  // update, already validated there) changes a cell's centroid and volume;
  // its id and face list never change after construction.
  friend class Mesh;
  void setGeometry(const Vector2& centroid, Real volume) noexcept {
    centroid_ = centroid;
    volume_ = volume;
  }

  Id id_{};
  Vector2 centroid_{};
  Real volume_{};
  std::vector<Index> faceIds_;
};

}  // namespace cfd::mesh
