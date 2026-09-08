#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"

namespace cfd::mesh {

// Groups boundary faces belonging to one physical boundary (e.g. "left",
// "top"). Describes mesh topology only -- what a boundary physically is
// (wall, inlet, moving wall, ...) is a BoundaryCondition concern, applied
// separately against a patch by name. Do not merge the two: a patch named
// "top" and a MovingWall boundary condition are different concepts.
class BoundaryPatch {
 public:
  BoundaryPatch(std::string name, std::vector<Index> faceIds);

  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] const std::vector<Index>& faceIds() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  std::string name_;
  std::vector<Index> faceIds_;
};

}  // namespace cfd::mesh
