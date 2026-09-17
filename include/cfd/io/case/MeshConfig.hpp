#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/MeshGrading.hpp"

namespace cfd::io {

// mesh.json. Two structured 2D quadrilateral mesh types, both with
// cell(i, j) = j * nx + i and the boundary patches "left", "right",
// "bottom", "top" (MeshGeometry.hpp):
//   - "structured_cartesian": uniform Cartesian cells over the geometry.json
//     rectangle (MeshGeometry::createCartesian2D); `vertices` is empty.
//   - "structured_quad" (P12-MESH-001): arbitrary, possibly non-orthogonal
//     quadrilaterals given by `vertices`, the (nx + 1) x (ny + 1) row-major
//     vertex grid (vertex(i, j) = vertices[j * (nx + 1) + i]) --
//     MeshGeometry::createStructuredQuad2D. Its boundary vertices must lie
//     on the geometry.json rectangle (CaseReader) and every cell must be a
//     strictly convex counter-clockwise quad (CaseBuilder).
//
// P12-MESH-002: an optional "grading" object (structured_cartesian only)
// grades the column widths along x and/or the row heights along y
// (cfd::mesh::AxisGrading, MeshGrading.hpp; MeshGeometry::createGraded2D).
// Absent -> `grading` is std::nullopt and the mesh is exactly the uniform
// Cartesian mesh it always was; an absent axis inside "grading" is uniform.
struct MeshGradingConfig {
  cfd::mesh::AxisGrading x;
  cfd::mesh::AxisGrading y;

  bool operator==(const MeshGradingConfig&) const = default;
};

// P12-MESH-003: "multiblock" -- conformal structured quadrilateral blocks
// (cfd::mesh::MultiBlockSpec, MeshGeometry::createMultiBlock2D) describing a
// genuinely non-rectangular domain; geometry.json is then
// {"type": "mesh_defined"}. nx / ny / vertices / grading are unused (0 /
// empty); the blocks carry their own. Block sides are named "left",
// "right", "bottom", "top" (MultiBlockSpec.hpp); patches are named by the
// case and are the case's boundary patches (boundaries.json must configure
// exactly these names).
struct MeshBlockConfig {
  std::string name;
  Index nx{};
  Index ny{};
  std::vector<Vector2> vertices;
};

struct MeshSideRefConfig {
  std::string block;
  std::string side;

  bool operator==(const MeshSideRefConfig&) const = default;
};

struct MeshInterfaceConfig {
  MeshSideRefConfig first;
  MeshSideRefConfig second;
  bool reversed{false};  // mesh.json "orientation": "aligned" (default) | "reversed"
};

struct MeshPatchConfig {
  std::string name;
  std::vector<MeshSideRefConfig> sides;
};

struct MeshConfig {
  std::string type;
  Index nx{};
  Index ny{};
  std::vector<Vector2> vertices;
  std::optional<MeshGradingConfig> grading;
  // P12-MESH-003, type "multiblock" only.
  std::vector<MeshBlockConfig> blocks;
  std::vector<MeshInterfaceConfig> interfaces;
  std::vector<MeshPatchConfig> patches;
  // P12-MESH-006: cells along z of a 3D "structured_cartesian" mesh over a
  // geometry.json "box" (MeshGeometry::createCartesian3D; uniform, no
  // grading). 0 = absent = a 2D mesh, exactly as before.
  Index nz{0};
};

// The boundary patch names a mesh of this configuration has: the case's
// named patches for "multiblock"; "xmin", "xmax", "ymin", "ymax", "zmin",
// "zmax" for a 3D mesh (nz > 0, P12-MESH-006); "left", "right", "bottom",
// "top" otherwise.
[[nodiscard]] std::vector<std::string> meshPatchNames(const MeshConfig& config);

// P12-MESH-002 vocabulary of mesh.json "grading" (one place for the parser,
// the writer and the GUI): the cluster names are the boundary patches the
// small cells are placed against -- x: "left" / "right" / "both",
// y: "bottom" / "top" / "both".
[[nodiscard]] const char* gradingTypeName(cfd::mesh::GradingType type) noexcept;
[[nodiscard]] const char* gradingClusterName(cfd::mesh::GradingCluster cluster,
                                             bool xAxis) noexcept;

}  // namespace cfd::io
