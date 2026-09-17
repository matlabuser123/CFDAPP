#pragma once

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::mesh {

// P12-MESH-003 -- a 2D multi-block structured mesh: conformal structured
// quadrilateral blocks joined along whole block sides, with named boundary
// patches assembled from whole block sides (MeshGeometry::createMultiBlock2D).
//
// Each block is a structured_quad vertex grid (StructuredGrid.hpp):
// (nx + 1) x (ny + 1) vertices, row-major, vertex(i, j) =
// vertices[j * (nx + 1) + i], every cell a strictly convex counter-clockwise
// quad. Its four sides, each traversed in increasing local index:
//   Bottom: vertex(i, 0),  i = 0..nx   (nx faces)
//   Top:    vertex(i, ny), i = 0..nx   (nx faces)
//   Left:   vertex(0, j),  j = 0..ny   (ny faces)
//   Right:  vertex(nx, j), j = 0..ny   (ny faces)
// Every side of every block is used exactly once: either in one interface
// or in one boundary patch (nothing is inferred from geometry).
enum class BlockSide { Left, Right, Bottom, Top };

struct BlockSideRef {
  Index block{0};  // index into MultiBlockSpec::blocks
  BlockSide side{BlockSide::Left};

  bool operator==(const BlockSideRef&) const = default;
};

struct BlockSpec {
  std::string name;
  Index nx{0};
  Index ny{0};
  std::vector<Vector2> vertices;
};

// A conformal interface: side `first` and side `second` have the same
// number of faces and IDENTICAL vertices (bitwise -- the case writes each
// shared vertex once per block with the same value), either in the same
// order (reversed = false: first's k-th vertex == second's k-th) or in
// opposite order (reversed = true: first's k-th == second's (N - k)-th). The
// interface becomes ordinary internal faces: owner = the `first` block's
// cell, neighbor = the `second` block's cell, one face per pair.
struct BlockInterfaceSpec {
  BlockSideRef first;
  BlockSideRef second;
  bool reversed{false};
};

struct BoundaryPatchSpec {
  std::string name;
  std::vector<BlockSideRef> sides;
};

struct MultiBlockSpec {
  std::vector<BlockSpec> blocks;
  std::vector<BlockInterfaceSpec> interfaces;
  std::vector<BoundaryPatchSpec> patches;
};

[[nodiscard]] const char* blockSideName(BlockSide side) noexcept;

}  // namespace cfd::mesh
