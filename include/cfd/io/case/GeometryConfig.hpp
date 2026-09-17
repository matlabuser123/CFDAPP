#pragma once

#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::io {

// P1 -- Case System scope matches what the numerical core actually
// supports (TODO.md P1 section 7): 2D, rectangular, Cartesian only. type
// is always "rectangle" for now -- kept as a string (rather than an enum
// with one value) so a future geometry type is an additive parser change,
// not a struct-shape break.
//
// P12-MESH-003: type "mesh_defined" -- the domain is whatever the
// mesh.json "multiblock" blocks cover (no length/height; both stay 0).
// Required by, and only valid with, a multiblock mesh.
//
// P12-MESH-006: type "box" -- the 3D domain [0, length] x [0, height] x
// [0, depth] (x, y, z), meshed by a structured_cartesian mesh with nz
// (MeshGeometry::createCartesian3D); `depth` is used only by a box.
struct GeometryConfig {
  std::string type;
  Real length{};
  Real height{};
  Real depth{};
};

// P12-MESH-006: the spatial dimension a geometry describes (3 for "box",
// 2 otherwise).
[[nodiscard]] inline int geometryDimension(const GeometryConfig& geometry) {
  return geometry.type == "box" ? 3 : 2;
}

}  // namespace cfd::io
