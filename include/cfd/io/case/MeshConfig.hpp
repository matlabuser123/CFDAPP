#pragma once

#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::io {

// Only the structured Cartesian generator the mesh layer already has
// (MeshGeometry::createCartesian2D) is supported in P1 -- type is always
// "structured_cartesian".
struct MeshConfig {
  std::string type;
  Index nx{};
  Index ny{};
};

}  // namespace cfd::io
