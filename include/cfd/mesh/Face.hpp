#pragma once

#include <optional>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::mesh {

// A finite-volume face -- fluxes pass through faces, so orientation is
// load-bearing. Convention (see PROJECT_STRUCTURE.md / docs): the area
// vector Sf = n * A points from owner toward neighbor for an internal
// face, and outward from the domain for a boundary face.
class Face {
 public:
  Face(Index id, Index owner, std::optional<Index> neighbor, Vector2 centroid, Vector2 areaVector);

  [[nodiscard]] Index id() const noexcept;
  [[nodiscard]] Index owner() const noexcept;
  [[nodiscard]] std::optional<Index> neighbor() const noexcept;

  [[nodiscard]] const Vector2& centroid() const noexcept;
  [[nodiscard]] const Vector2& areaVector() const noexcept;

  [[nodiscard]] Real area() const noexcept;
  [[nodiscard]] bool isBoundary() const noexcept;

 private:
  Index id_{};
  Index owner_{};
  std::optional<Index> neighbor_;

  Vector2 centroid_{};
  Vector2 areaVector_{};
};

}  // namespace cfd::mesh
