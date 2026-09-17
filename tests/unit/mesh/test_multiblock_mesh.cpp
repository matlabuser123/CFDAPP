// P12-MESH-003: MeshGeometry::createMultiBlock2D -- conformal multi-block
// structured quadrilateral meshes of general (non-rectangular, multiply
// connected) 2D domains.
//
// Connectivity is checked by invariants that do not depend on any solver:
// every cell has four faces that list it, cell closure, owner-to-neighbour
// normal orientation, outward boundary normals, patch coverage, face/area
// counts, and the Euler characteristic V - E + F = 1 - (number of holes).
// Interfaces are checked against the single-block mesh of the same geometry
// (identical cells, faces and two-point coefficients) and for owner /
// neighbour flux bookkeeping. Every rejected configuration names what is
// wrong.
#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::mesh::BlockInterfaceSpec;
using cfd::mesh::BlockSide;
using cfd::mesh::BlockSideRef;
using cfd::mesh::BlockSpec;
using cfd::mesh::BoundaryPatchSpec;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::mesh::MeshQuality;
using cfd::mesh::MultiBlockSpec;

namespace {

constexpr BlockSide kLeft = BlockSide::Left;
constexpr BlockSide kRight = BlockSide::Right;
constexpr BlockSide kBottom = BlockSide::Bottom;
constexpr BlockSide kTop = BlockSide::Top;
const Real kPi = std::acos(-1.0);

BlockSpec blockFrom(const std::string& name, Index nx, Index ny,
                    const std::function<Vector2(Index, Index)>& vertex) {
  BlockSpec block{name, nx, ny, {}};
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) block.vertices.push_back(vertex(i, j));
  }
  return block;
}

// Axis-aligned block [x0, x1] x [y0, y1] (local i = +x, j = +y).
BlockSpec rectBlock(const std::string& name, Real x0, Real x1, Real y0, Real y1, Index nx,
                    Index ny) {
  return blockFrom(name, nx, ny, [=](Index i, Index j) {
    return Vector2{x0 + ((x1 - x0) * static_cast<Real>(i) / static_cast<Real>(nx)),
                   y0 + ((y1 - y0) * static_cast<Real>(j) / static_cast<Real>(ny))};
  });
}

BlockSideRef ref(Index block, BlockSide side) { return BlockSideRef{block, side}; }

// [0, 2] x [0, 1] as two 3 x 2 blocks joined at x = 1.
MultiBlockSpec twoBlockRectangle() {
  MultiBlockSpec spec;
  spec.blocks = {rectBlock("a", 0.0, 1.0, 0.0, 1.0, 3, 2),
                 rectBlock("b", 1.0, 2.0, 0.0, 1.0, 3, 2)};
  spec.interfaces = {BlockInterfaceSpec{ref(0, kRight), ref(1, kLeft), false}};
  spec.patches = {{"left", {ref(0, kLeft)}},
                  {"right", {ref(1, kRight)}},
                  {"bottom", {ref(0, kBottom), ref(1, kBottom)}},
                  {"top", {ref(0, kTop), ref(1, kTop)}}};
  return spec;
}

// L-shaped step channel: upstream [0, 1] x [0.5, 1], upper [1, 3] x [0.5, 1],
// lower [1, 3] x [0, 0.5].
MultiBlockSpec lShape() {
  MultiBlockSpec spec;
  spec.blocks = {rectBlock("upstream", 0.0, 1.0, 0.5, 1.0, 4, 2),
                 rectBlock("upper", 1.0, 3.0, 0.5, 1.0, 8, 2),
                 rectBlock("lower", 1.0, 3.0, 0.0, 0.5, 8, 2)};
  spec.interfaces = {BlockInterfaceSpec{ref(0, kRight), ref(1, kLeft), false},
                     BlockInterfaceSpec{ref(2, kTop), ref(1, kBottom), false}};
  spec.patches = {{"inlet", {ref(0, kLeft)}},
                  {"outlet", {ref(1, kRight), ref(2, kRight)}},
                  {"top_wall", {ref(0, kTop), ref(1, kTop)}},
                  {"bottom_wall", {ref(0, kBottom), ref(2, kBottom)}},
                  {"step", {ref(2, kLeft)}}};
  return spec;
}

// [0, 3]^2 minus the solid [1, 2]^2: eight 2 x 2 blocks around the hole.
MultiBlockSpec squareWithHole() {
  MultiBlockSpec spec;
  std::map<std::pair<int, int>, Index> id;
  for (int bj = 0; bj < 3; ++bj) {
    for (int bi = 0; bi < 3; ++bi) {
      if (bi == 1 && bj == 1) continue;
      id[{bi, bj}] = spec.blocks.size();
      spec.blocks.push_back(
          rectBlock("b" + std::to_string(bi) + std::to_string(bj), bi, bi + 1, bj, bj + 1, 2, 2));
    }
  }
  const auto at = [&](int bi, int bj) { return id.at({bi, bj}); };
  for (int bj : {0, 2}) {
    spec.interfaces.push_back({ref(at(0, bj), kRight), ref(at(1, bj), kLeft), false});
    spec.interfaces.push_back({ref(at(1, bj), kRight), ref(at(2, bj), kLeft), false});
  }
  for (int bi : {0, 2}) {
    spec.interfaces.push_back({ref(at(bi, 0), kTop), ref(at(bi, 1), kBottom), false});
    spec.interfaces.push_back({ref(at(bi, 1), kTop), ref(at(bi, 2), kBottom), false});
  }
  BoundaryPatchSpec outer{"outer", {}};
  for (int bj = 0; bj < 3; ++bj) {
    outer.sides.push_back(ref(at(0, bj), kLeft));
    outer.sides.push_back(ref(at(2, bj), kRight));
  }
  for (int bi = 0; bi < 3; ++bi) {
    outer.sides.push_back(ref(at(bi, 0), kBottom));
    outer.sides.push_back(ref(at(bi, 2), kTop));
  }
  spec.patches = {
      outer,
      {"hole",
       {ref(at(1, 0), kTop), ref(at(0, 1), kRight), ref(at(2, 1), kLeft), ref(at(1, 2), kBottom)}}};
  return spec;
}

// Annular blocks r in [1, 2] (local i = radius, j = angle) over `sweep`,
// `blocks` blocks of nt angular cells; closeRing joins the last block's top
// to the first block's bottom (a full O-grid ring; its vertices on theta =
// 2 pi reuse the theta = 0 values so the interface is exact).
MultiBlockSpec annulus(Index nr, Index nt, Index blocks, Real sweep, bool closeRing) {
  const Index total = blocks * nt;
  const auto point = [=](Index i, Index jGlobal) {
    const Index j = (closeRing && jGlobal == total) ? 0 : jGlobal;
    const Real r = 1.0 + (static_cast<Real>(i) / static_cast<Real>(nr));
    const Real t = sweep * static_cast<Real>(j) / static_cast<Real>(total);
    return Vector2{r * std::cos(t), r * std::sin(t)};
  };
  MultiBlockSpec spec;
  BoundaryPatchSpec inner{"inner", {}};
  BoundaryPatchSpec outer{"outer", {}};
  for (Index b = 0; b < blocks; ++b) {
    spec.blocks.push_back(blockFrom("s" + std::to_string(b), nr, nt,
                                    [=](Index i, Index j) { return point(i, (b * nt) + j); }));
    inner.sides.push_back(ref(b, kLeft));
    outer.sides.push_back(ref(b, kRight));
    if (b + 1 < blocks) spec.interfaces.push_back({ref(b, kTop), ref(b + 1, kBottom), false});
  }
  spec.patches = {inner, outer};
  if (closeRing) {
    spec.interfaces.push_back({ref(blocks - 1, kTop), ref(0, kBottom), false});
  } else {
    spec.patches.push_back({"start", {ref(0, kBottom)}});
    spec.patches.push_back({"end", {ref(blocks - 1, kTop)}});
  }
  return spec;
}

std::string buildError(const MultiBlockSpec& spec) {
  try {
    (void)MeshGeometry::createMultiBlock2D(spec);
  } catch (const InvalidArgumentError& e) {
    return e.what();
  }
  return "no error";
}

// Solver-independent connectivity and orientation invariants; returns the
// Euler characteristic V - E + F (vertices counted by exact coordinates).
int checkInvariants(const Mesh& mesh, const MultiBlockSpec& spec, Real expectedArea) {
  const auto& cells = mesh.cells();
  const auto& faces = mesh.faces();
  // Cells: four faces, each listing the cell; closure; positive volume.
  Real area = 0.0;
  for (const auto& cell : cells) {
    EXPECT_EQ(cell.faceIds().size(), 4u) << "cell " << cell.id();
    Vector2 closure{0.0, 0.0};
    Real scale = 0.0;
    for (const Index f : cell.faceIds()) {
      const auto& face = faces[f];
      const bool owner = face.owner() == cell.id();
      EXPECT_TRUE(owner || face.neighbor() == cell.id()) << "cell " << cell.id() << " face " << f;
      closure += owner ? face.areaVector() : -face.areaVector();
      scale += face.area();
    }
    EXPECT_LE(magnitude(closure), 1e-13 * scale) << "cell " << cell.id();
    EXPECT_GT(cell.volume(), 0.0);
    area += cell.volume();
  }
  EXPECT_NEAR(area, expectedArea, 1e-12 * expectedArea);
  // Faces: internal normals point owner -> neighbour, boundary normals out.
  Index boundaryFaces = 0;
  for (const auto& face : faces) {
    EXPECT_EQ(faces[face.id()].id(), face.id());
    const Vector2& xp = cells[face.owner()].centroid();
    if (face.isBoundary()) {
      ++boundaryFaces;
      EXPECT_GT(dot(face.centroid() - xp, face.areaVector()), 0.0) << "boundary face " << face.id();
    } else {
      EXPECT_NE(*face.neighbor(), face.owner());
      EXPECT_GT(dot(cells[*face.neighbor()].centroid() - xp, face.areaVector()), 0.0)
          << "internal face " << face.id();
    }
  }
  // Patches: every boundary face in exactly one patch, nothing else.
  std::vector<int> inPatch(faces.size(), 0);
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index f : patch.faceIds()) {
      EXPECT_TRUE(faces[f].isBoundary()) << patch.name() << " face " << f;
      ++inPatch[f];
    }
  }
  for (const auto& face : faces) EXPECT_EQ(inPatch[face.id()], face.isBoundary() ? 1 : 0);
  // Counts from the specification.
  Index cellCount = 0, internal = 0, sides = 0;
  for (const auto& block : spec.blocks) {
    cellCount += block.nx * block.ny;
    internal += ((block.nx - 1) * block.ny) + (block.nx * (block.ny - 1));
  }
  for (const auto& iface : spec.interfaces) {
    const auto& b = spec.blocks[iface.first.block];
    internal += (iface.first.side == kLeft || iface.first.side == kRight) ? b.ny : b.nx;
  }
  for (const auto& patch : spec.patches) {
    for (const auto& side : patch.sides) {
      const auto& b = spec.blocks[side.block];
      sides += (side.side == kLeft || side.side == kRight) ? b.ny : b.nx;
    }
  }
  EXPECT_EQ(mesh.numberOfCells(), cellCount);
  EXPECT_EQ(faces.size() - boundaryFaces, internal);
  EXPECT_EQ(boundaryFaces, sides);
  // Production validity gate: valid and one connected region.
  const auto quality = MeshQuality::evaluate(mesh);
  EXPECT_TRUE(quality.valid);
  for (const auto& problem : quality.problems) ADD_FAILURE() << problem;
  EXPECT_EQ(quality.connectedComponents, 1u);
  // Euler characteristic.
  std::set<std::pair<Real, Real>> vertices;
  for (const auto& block : spec.blocks) {
    for (const auto& v : block.vertices) vertices.insert({v.x, v.y});
  }
  return static_cast<int>(vertices.size()) - static_cast<int>(faces.size()) +
         static_cast<int>(cells.size());
}

const cfd::mesh::BoundaryPatch& patchNamed(const Mesh& mesh, const std::string& name) {
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == name) return patch;
  }
  throw std::runtime_error("no patch " + name);
}

}  // namespace

TEST(MultiBlockMesh, TwoBlockRectangleIsTheSingleBlockMeshRenumbered) {
  const MultiBlockSpec spec = twoBlockRectangle();
  const Mesh multi = MeshGeometry::createMultiBlock2D(spec);
  EXPECT_EQ(checkInvariants(multi, spec, 2.0), 1);

  std::vector<Vector2> vertices;
  for (Index j = 0; j <= 2; ++j) {
    for (Index i = 0; i <= 6; ++i) {
      // The same arithmetic as the blocks' own vertices.
      const Real x =
          i <= 3 ? 1.0 * static_cast<Real>(i) / 3.0 : 1.0 + (1.0 * static_cast<Real>(i - 3) / 3.0);
      vertices.push_back(Vector2{x, static_cast<Real>(j) / 2.0});
    }
  }
  const Mesh single = MeshGeometry::createStructuredQuad2D(6, 2, vertices);
  ASSERT_EQ(multi.numberOfCells(), single.numberOfCells());
  ASSERT_EQ(multi.numberOfFaces(), single.numberOfFaces());
  // Cells: identical geometry (bitwise), matched by centroid.
  std::map<std::pair<Real, Real>, Index> singleCell;
  for (const auto& c : single.cells()) singleCell[{c.centroid().x, c.centroid().y}] = c.id();
  std::vector<Index> toSingle(multi.numberOfCells());
  for (const auto& c : multi.cells()) {
    const auto it = singleCell.find({c.centroid().x, c.centroid().y});
    ASSERT_NE(it, singleCell.end()) << "cell " << c.id();
    EXPECT_EQ(single.cell(it->second).volume(), c.volume());
    toSingle[c.id()] = it->second;
  }
  // Faces: identical centroid, area vector and (mapped) owner/neighbour --
  // the interface faces are exactly the single block's internal faces.
  std::map<std::pair<Real, Real>, Index> singleFace;
  for (const auto& f : single.faces()) singleFace[{f.centroid().x, f.centroid().y}] = f.id();
  for (const auto& f : multi.faces()) {
    const auto it = singleFace.find({f.centroid().x, f.centroid().y});
    ASSERT_NE(it, singleFace.end()) << "face " << f.id();
    const auto& s = single.face(it->second);
    EXPECT_EQ(s.areaVector().x, f.areaVector().x);
    EXPECT_EQ(s.areaVector().y, f.areaVector().y);
    EXPECT_EQ(s.owner(), toSingle[f.owner()]);
    EXPECT_EQ(s.isBoundary(), f.isBoundary());
    if (!f.isBoundary()) {
      EXPECT_EQ(*s.neighbor(), toSingle[*f.neighbor()]);
    }
  }
  // Patches: the same faces under the same names.
  for (const char* name : {"left", "right", "bottom", "top"}) {
    std::set<Index> a, b;
    for (const Index f : patchNamed(multi, name).faceIds()) {
      a.insert(singleFace.at({multi.face(f).centroid().x, multi.face(f).centroid().y}));
    }
    for (const Index f : patchNamed(single, name).faceIds()) b.insert(f);
    EXPECT_EQ(a, b) << name;
  }
}

TEST(MultiBlockMesh, InterfaceFacesAreSingleInternalFacesOwnedByTheFirstBlock) {
  const MultiBlockSpec spec = twoBlockRectangle();
  const Mesh mesh = MeshGeometry::createMultiBlock2D(spec);
  // Block a = cells 0..5, block b = cells 6..11 (block order, local j * nx + i).
  Index interfaceFaces = 0;
  for (const auto& f : mesh.faces()) {
    if (f.isBoundary() || f.centroid().x != 1.0) continue;
    ++interfaceFaces;
    EXPECT_LT(f.owner(), 6u);
    EXPECT_GE(*f.neighbor(), 6u);
    EXPECT_EQ(f.owner() % 3, 2u);            // a's i = nx - 1 column
    EXPECT_EQ((*f.neighbor() - 6) % 3, 0u);  // b's i = 0 column
    EXPECT_NEAR(mesh.cell(f.owner()).centroid().y, mesh.cell(*f.neighbor()).centroid().y, 1e-15);
    EXPECT_GT(f.areaVector().x, 0.0);  // a -> b
  }
  EXPECT_EQ(interfaceFaces, 2u);
  // No boundary face lies on the interface (no duplicated one-sided faces).
  for (const auto& f : mesh.faces()) {
    if (f.isBoundary()) {
      EXPECT_NE(f.centroid().x, 1.0) << f.id();
    }
  }
}

// Blocks in rotated local frames: interfaces between different side kinds
// and with reversed traversal connect the geometrically adjacent cells.
TEST(MultiBlockMesh, RotatedBlocksAndReversedInterfacesConnectAdjacentCells) {
  for (const int variant : {0, 1}) {
    MultiBlockSpec spec;
    spec.blocks.push_back(rectBlock("a", 0.0, 1.0, 0.0, 1.0, 3, 2));
    if (variant == 0) {
      // b rotated 180 degrees: local i = -x, j = -y; its right side runs
      // down x = 1 -> interface a.right <-> b.right, reversed.
      spec.blocks.push_back(blockFrom("b", 3, 2, [](Index i, Index j) {
        return Vector2{2.0 - (static_cast<Real>(i) / 3.0), 1.0 - (static_cast<Real>(j) / 2.0)};
      }));
      spec.interfaces = {{ref(0, kRight), ref(1, kRight), true}};
      spec.patches = {{"walls",
                       {ref(0, kLeft), ref(0, kBottom), ref(0, kTop), ref(1, kLeft),
                        ref(1, kBottom), ref(1, kTop)}}};
    } else {
      // b rotated 90 degrees: local i = +y, j = -x; its top side runs up
      // x = 1 -> interface a.right <-> b.top, aligned.
      spec.blocks.push_back(blockFrom("b", 2, 3, [](Index i, Index j) {
        return Vector2{2.0 - (static_cast<Real>(j) / 3.0), static_cast<Real>(i) / 2.0};
      }));
      spec.interfaces = {{ref(0, kRight), ref(1, kTop), false}};
      spec.patches = {{"walls",
                       {ref(0, kLeft), ref(0, kBottom), ref(0, kTop), ref(1, kLeft), ref(1, kRight),
                        ref(1, kBottom)}}};
    }
    const Mesh mesh = MeshGeometry::createMultiBlock2D(spec);
    EXPECT_EQ(checkInvariants(mesh, spec, 2.0), 1) << variant;
    Index interfaceFaces = 0;
    for (const auto& f : mesh.faces()) {
      if (f.isBoundary() || f.owner() >= 6 || *f.neighbor() < 6) continue;
      ++interfaceFaces;
      const Vector2 d = mesh.cell(*f.neighbor()).centroid() - mesh.cell(f.owner()).centroid();
      EXPECT_NEAR(d.x, 1.0 / 3.0, 1e-15) << variant;  // the cell directly across x = 1
      EXPECT_NEAR(d.y, 0.0, 1e-15) << variant;
    }
    EXPECT_EQ(interfaceFaces, 2u) << variant;
    // Declaring the wrong traversal direction is rejected, not silently
    // mis-connected.
    spec.interfaces[0].reversed = !spec.interfaces[0].reversed;
    EXPECT_NE(buildError(spec).find("does not coincide"), std::string::npos) << variant;
  }
}

TEST(MultiBlockMesh, LShapedDomainPatchesFollowTheListedSides) {
  const MultiBlockSpec spec = lShape();
  const Mesh mesh = MeshGeometry::createMultiBlock2D(spec);
  EXPECT_EQ(checkInvariants(mesh, spec, 2.5), 1);  // 0.5 + 1 + 1 (the 3 x 1 box minus 0.5)
  // No cell in the removed corner [0, 1] x [0, 0.5].
  for (const auto& c : mesh.cells()) {
    EXPECT_FALSE(c.centroid().x < 1.0 && c.centroid().y < 0.5) << c.id();
  }
  // Patch order: listed sides in order, each in increasing local index.
  const auto& outlet = patchNamed(mesh, "outlet");
  ASSERT_EQ(outlet.faceIds().size(), 4u);
  const Real expectedY[] = {0.625, 0.875, 0.125, 0.375};
  for (std::size_t k = 0; k < 4; ++k) {
    EXPECT_EQ(mesh.face(outlet.faceIds()[k]).centroid().x, 3.0);
    EXPECT_EQ(mesh.face(outlet.faceIds()[k]).centroid().y, expectedY[k]);
  }
  const auto& step = patchNamed(mesh, "step");
  ASSERT_EQ(step.faceIds().size(), 2u);
  for (const Index f : step.faceIds()) {
    EXPECT_EQ(mesh.face(f).centroid().x, 1.0);
    EXPECT_LT(mesh.face(f).areaVector().x, 0.0);  // outward = -x, into the solid step
  }
  // Grids: one per block, named, in block order; no single structured grid.
  ASSERT_EQ(mesh.structuredBlocks().size(), 3u);
  EXPECT_EQ(mesh.structuredBlocks()[1].name, "upper");
  EXPECT_EQ(mesh.structuredBlocks()[1].nx, 8u);
  EXPECT_EQ(mesh.structuredBlocks()[1].vertices, spec.blocks[1].vertices);
  EXPECT_EQ(mesh.structuredGrid(), nullptr);
}

// Internal solid region: the hole is simply not meshed; its sides are a
// wall patch and no cell lies inside it.
TEST(MultiBlockMesh, InternalSolidRegionIsAHoleBoundedByItsPatch) {
  const MultiBlockSpec spec = squareWithHole();
  const Mesh mesh = MeshGeometry::createMultiBlock2D(spec);
  EXPECT_EQ(checkInvariants(mesh, spec, 8.0), 0);  // one hole: V - E + F = 0
  for (const auto& c : mesh.cells()) {
    const Vector2& x = c.centroid();
    EXPECT_FALSE(x.x > 1.0 && x.x < 2.0 && x.y > 1.0 && x.y < 2.0) << c.id();
  }
  const auto& hole = patchNamed(mesh, "hole");
  ASSERT_EQ(hole.faceIds().size(), 8u);
  Vector2 sum{0.0, 0.0};
  for (const Index f : hole.faceIds()) {
    const auto& face = mesh.face(f);
    // Outward from the fluid = into the solid square (towards its centre).
    EXPECT_GT(dot(Vector2{1.5, 1.5} - face.centroid(), face.areaVector()), 0.0);
    sum += face.areaVector();
  }
  EXPECT_NEAR(magnitude(sum), 0.0, 1e-15);  // a closed inner boundary
  EXPECT_EQ(patchNamed(mesh, "outer").faceIds().size(), 24u);
}

TEST(MultiBlockMesh, CurvedSectorsAndClosedRing) {
  // 270-degree annular sector from three blocks (non-convex domain).
  const MultiBlockSpec sector = annulus(4, 6, 3, 1.5 * kPi, false);
  const Mesh sectorMesh = MeshGeometry::createMultiBlock2D(sector);
  // Area of the chordal polygon: 18 segments of 0.5 (r2^2 - r1^2) sin(dtheta).
  const Real chordArea = 18.0 * 0.5 * 3.0 * std::sin(1.5 * kPi / 18.0);
  EXPECT_EQ(checkInvariants(sectorMesh, sector, chordArea), 1);

  // Full ring: an O-grid with the last block joined to the first (a
  // multiply connected domain, one hole).
  for (const Index blocks : {Index{1}, Index{4}}) {
    const MultiBlockSpec ring = annulus(3, 16 / blocks, blocks, 2.0 * kPi, true);
    const Mesh ringMesh = MeshGeometry::createMultiBlock2D(ring);
    Real a = 0.0;
    for (const auto& c : ringMesh.cells()) a += c.volume();
    EXPECT_EQ(checkInvariants(ringMesh, ring, a), 0) << blocks;
    EXPECT_TRUE(ringMesh.boundaryPatches().size() == 2u);
  }
}

// Owner/neighbour bookkeeping across interfaces: a face flux is one stored
// value; the owner block counts it out, the neighbour block in -- block
// balances of any face-flux field cancel exactly at the interface.
TEST(MultiBlockMesh, InterfaceFluxesCancelExactlyBetweenBlocks) {
  const MultiBlockSpec spec = lShape();
  const Mesh mesh = MeshGeometry::createMultiBlock2D(spec);
  std::vector<Index> blockOf(mesh.numberOfCells());
  Index offset = 0;
  for (Index b = 0; b < spec.blocks.size(); ++b) {
    for (Index k = 0; k < spec.blocks[b].nx * spec.blocks[b].ny; ++k) blockOf[offset + k] = b;
    offset += spec.blocks[b].nx * spec.blocks[b].ny;
  }
  // An arbitrary non-uniform face-flux field (positive along the face normal).
  std::vector<Real> flux(mesh.numberOfFaces());
  for (const auto& f : mesh.faces()) {
    flux[f.id()] = std::sin(3.0 * f.centroid().x) + (0.3 * f.centroid().y) +
                   (1e-3 * static_cast<Real>(f.id()));
  }
  // Block balance as the cells see it (each cell's own face list, outward
  // sign) ...
  std::vector<Real> fromCells(3, 0.0);
  for (const auto& c : mesh.cells()) {
    for (const Index f : c.faceIds()) {
      fromCells[blockOf[c.id()]] += (mesh.face(f).owner() == c.id() ? 1.0 : -1.0) * flux[f];
    }
  }
  // ... equals the block's boundary flux plus its interface flux, with each
  // interface face counted out of the owner block and into the neighbour
  // block -- the same stored value on both sides.
  std::vector<Real> fromFaces(3, 0.0);
  Index interfaceFaces = 0;
  for (const auto& f : mesh.faces()) {
    if (f.isBoundary()) {
      fromFaces[blockOf[f.owner()]] += flux[f.id()];
    } else if (blockOf[f.owner()] != blockOf[*f.neighbor()]) {
      ++interfaceFaces;
      fromFaces[blockOf[f.owner()]] += flux[f.id()];
      fromFaces[blockOf[*f.neighbor()]] -= flux[f.id()];
    }
  }
  EXPECT_EQ(interfaceFaces, 2u + 8u);  // upstream|upper (2) + lower|upper (8)
  for (Index b = 0; b < 3; ++b) EXPECT_NEAR(fromCells[b], fromFaces[b], 1e-12) << b;
  // A uniform velocity has zero net flux out of every cell (closure) and of
  // every block.
  const Vector2 u{0.7, -0.4};
  for (const auto& c : mesh.cells()) {
    Real net = 0.0;
    for (const Index f : c.faceIds()) {
      const auto& face = mesh.face(f);
      net += (face.owner() == c.id() ? 1.0 : -1.0) * dot(u, face.areaVector());
    }
    EXPECT_NEAR(net, 0.0, 1e-15) << c.id();
  }
}

// ---------------------------------------------------------------------------
// Rejected configurations
// ---------------------------------------------------------------------------

TEST(MultiBlockMeshInvalid, StructuralErrorsAreNamed) {
  const auto expectError = [](MultiBlockSpec spec, const std::string& fragment, const char* what) {
    const std::string message = buildError(spec);
    EXPECT_NE(message.find(fragment), std::string::npos) << what << ": " << message;
  };
  expectError(MultiBlockSpec{}, "at least one block", "no blocks");
  {
    auto s = twoBlockRectangle();
    s.blocks[1].name = "a";
    expectError(s, "duplicate block name 'a'", "duplicate block");
  }
  {
    auto s = twoBlockRectangle();
    s.blocks[0].name.clear();
    expectError(s, "has no name", "unnamed block");
  }
  {
    auto s = twoBlockRectangle();
    s.blocks[0].vertices.pop_back();
    expectError(s, "needs (nx + 1) * (ny + 1) = 12 vertices, got 11", "vertex count");
  }
  {
    auto s = twoBlockRectangle();
    s.blocks[0].vertices[5].x = std::numeric_limits<Real>::quiet_NaN();
    expectError(s, "non-finite", "NaN vertex");
  }
  {
    auto s = twoBlockRectangle();
    s.patches[2].sides.push_back(ref(0, kTop));
    expectError(s, "block 'a' side 'top' is used more than once", "side twice");
  }
  {
    auto s = twoBlockRectangle();
    s.patches[0].sides.push_back(ref(0, kRight));  // also the interface side
    expectError(s, "block 'a' side 'right' is used more than once", "interface + patch");
  }
  {
    auto s = twoBlockRectangle();
    s.patches[3].sides.pop_back();
    expectError(s, "block 'b' side 'top' is neither an interface nor part of a boundary patch",
                "unused side");
  }
  {
    auto s = twoBlockRectangle();
    s.patches[0].sides[0].block = 7;
    expectError(s, "non-existent block", "bad block reference");
  }
  {
    auto s = twoBlockRectangle();
    s.patches[1].name = "left";
    expectError(s, "duplicate patch name 'left'", "duplicate patch");
  }
  {
    auto s = twoBlockRectangle();
    s.patches.push_back({"empty", {}});
    expectError(s, "patch 'empty' has no block sides", "empty patch");
  }
}

TEST(MultiBlockMeshInvalid, InvalidCellsAreNamed) {
  {
    // Clockwise (mirrored) block: every cell inverted.
    MultiBlockSpec s;
    s.blocks = {blockFrom("b", 3, 2, [](Index i, Index j) {
      return Vector2{1.0 + (static_cast<Real>(i) / 3.0), 1.0 - (static_cast<Real>(j) / 2.0)};
    })};
    s.patches = {{"walls", {ref(0, kLeft), ref(0, kRight), ref(0, kBottom), ref(0, kTop)}}};
    EXPECT_NE(buildError(s).find("block 'b' cell (0,0) is not a strictly convex counter-clockwise"),
              std::string::npos)
        << buildError(s);
  }
  {
    // One interior vertex pushed through its neighbour: a non-convex cell.
    auto s = twoBlockRectangle();
    s.blocks[0].vertices[(1 * 4) + 1] = Vector2{0.75, 0.5};
    EXPECT_NE(buildError(s).find("block 'a' cell ("), std::string::npos) << buildError(s);
  }
}

TEST(MultiBlockMeshInvalid, NonConformalOrMismatchedInterfacesAreRejected) {
  {
    auto s = twoBlockRectangle();
    s.blocks[1] = rectBlock("b", 1.0, 2.0, 0.0, 1.0, 3, 3);  // 3 faces vs 2
    const std::string m = buildError(s);
    EXPECT_NE(m.find("has 2 faces but block 'b' side 'left' has 3"), std::string::npos) << m;
    EXPECT_NE(m.find("conformal"), std::string::npos) << m;
  }
  {
    // A visible gap between the blocks.
    auto s = twoBlockRectangle();
    s.blocks[1] = rectBlock("b", 1.001, 2.0, 0.0, 1.0, 3, 2);
    EXPECT_NE(buildError(s).find("does not coincide"), std::string::npos) << buildError(s);
  }
  {
    // One ulp is still not the same vertex: interfaces are exact.
    auto s = twoBlockRectangle();
    s.blocks[1].vertices[(1 * 4) + 0].x = std::nextafter(1.0, 2.0);
    const std::string m = buildError(s);
    EXPECT_NE(m.find("vertex 1 of block 'a' side 'right'"), std::string::npos) << m;
  }
  {
    // Interface declared on two sides that are not adjacent at all.
    auto s = twoBlockRectangle();
    s.interfaces = {{ref(0, kRight), ref(1, kRight), false}};
    s.patches[0].sides.push_back(ref(1, kLeft));
    s.patches[1].sides = {ref(0, kLeft)};
    s.patches[0].sides.erase(s.patches[0].sides.begin());
    EXPECT_NE(buildError(s).find("does not coincide"), std::string::npos) << buildError(s);
  }
}

TEST(MultiBlockMeshInvalid, OverlapsAndUndeclaredContactsAreRejected) {
  {
    // Two blocks touching along a side, but no interface declared: the
    // domain would have a zero-thickness internal wall.
    auto s = twoBlockRectangle();
    s.interfaces.clear();
    s.patches.push_back({"a_right", {ref(0, kRight)}});
    s.patches.push_back({"b_left", {ref(1, kLeft)}});
    const std::string m = buildError(s);
    EXPECT_NE(m.find("coincide"), std::string::npos) << m;
    EXPECT_NE(m.find("must be joined by an interface"), std::string::npos) << m;
  }
  {
    // Touching with different face counts and no interface (hanging vertices).
    auto s = twoBlockRectangle();
    s.interfaces.clear();
    s.blocks[1] = rectBlock("b", 1.0, 2.0, 0.0, 1.0, 3, 3);
    s.patches.push_back({"a_right", {ref(0, kRight)}});
    s.patches.push_back({"b_left", {ref(1, kLeft)}});
    const std::string m = buildError(s);
    EXPECT_TRUE(m.find("overlap") != std::string::npos ||
                m.find("cross or touch") != std::string::npos)
        << m;
  }
  {
    // Partially overlapping blocks.
    auto s = twoBlockRectangle();
    s.interfaces.clear();
    s.blocks[1] = rectBlock("b", 0.5, 1.5, 0.0, 1.0, 3, 2);
    s.patches.push_back({"a_right", {ref(0, kRight)}});
    s.patches.push_back({"b_left", {ref(1, kLeft)}});
    const std::string m = buildError(s);
    EXPECT_NE(m.find("cross or touch"), std::string::npos) << m;
  }
  {
    // A duplicated block.
    auto s = twoBlockRectangle();
    s.interfaces.clear();
    s.blocks[1] = rectBlock("b", 0.0, 1.0, 0.0, 1.0, 3, 2);
    s.patches.push_back({"a_right", {ref(0, kRight)}});
    s.patches.push_back({"b_left", {ref(1, kLeft)}});
    EXPECT_NE(buildError(s).find("coincide"), std::string::npos) << buildError(s);
  }
  {
    // A block entirely inside another, boundaries not touching: caught by
    // the winding number (its cells are covered twice).
    auto s = twoBlockRectangle();
    s.interfaces.clear();
    s.blocks[1] = rectBlock("b", 0.4, 0.6, 0.3, 0.7, 1, 1);
    s.patches.push_back({"a_right", {ref(0, kRight)}});
    s.patches.push_back({"b_left", {ref(1, kLeft)}});
    const std::string m = buildError(s);
    EXPECT_NE(m.find("covered 2 times"), std::string::npos) << m;
    EXPECT_NE(m.find("blocks overlap or fold over each other"), std::string::npos) << m;
  }
  {
    // A corner of one block touching the middle of another block's face.
    // (A diamond whose left corner (1, 0.3) lies inside a.right's first face.)
    auto s = twoBlockRectangle();
    s.interfaces.clear();
    s.blocks[1] = blockFrom("b", 1, 1, [](Index i, Index j) {
      const Vector2 corners[2][2] = {{Vector2{1.0, 0.3}, Vector2{1.5, -0.2}},
                                     {Vector2{1.5, 0.8}, Vector2{2.0, 0.3}}};
      return corners[j][i];
    });
    s.patches.push_back({"a_right", {ref(0, kRight)}});
    s.patches.push_back({"b_left", {ref(1, kLeft)}});
    const std::string m = buildError(s);
    EXPECT_NE(m.find("cross or touch"), std::string::npos) << m;
  }
  {
    // A single block wound past 360 degrees overlaps itself.
    const MultiBlockSpec s = annulus(2, 30, 1, 2.2 * kPi, false);
    const std::string m = buildError(s);
    EXPECT_TRUE(m.find("cross or touch") != std::string::npos ||
                m.find("overlap") != std::string::npos)
        << m;
  }
}

// A domain made of pieces that do not touch builds geometrically, but the
// production validity gate rejects it as more than one connected region.
TEST(MultiBlockMeshInvalid, DisconnectedDomainFailsTheValidityGate) {
  MultiBlockSpec s;
  s.blocks = {rectBlock("a", 0.0, 1.0, 0.0, 1.0, 2, 2), rectBlock("b", 2.0, 3.0, 0.0, 1.0, 3, 2)};
  s.patches = {{"walls",
                {ref(0, kLeft), ref(0, kRight), ref(0, kBottom), ref(0, kTop), ref(1, kLeft),
                 ref(1, kRight), ref(1, kBottom), ref(1, kTop)}}};
  const Mesh mesh = MeshGeometry::createMultiBlock2D(s);
  const auto quality = MeshQuality::evaluate(mesh);
  EXPECT_FALSE(quality.valid);
  EXPECT_EQ(quality.connectedComponents, 2u);
  EXPECT_EQ(quality.componentCellCounts, (std::vector<Index>{6, 4}));
  bool reported = false;
  for (const auto& p : quality.problems) {
    reported =
        reported || p.find("mesh has 2 disconnected cell regions (cells per region: 6, 4)") !=
                        std::string::npos;
  }
  EXPECT_TRUE(reported);
  // Blocks meeting only at one corner are still two regions.
  s.blocks[1] = rectBlock("b", 1.0, 2.0, 1.0, 2.0, 3, 2);
  const auto corner = MeshQuality::evaluate(MeshGeometry::createMultiBlock2D(s));
  EXPECT_EQ(corner.connectedComponents, 2u);
  EXPECT_FALSE(corner.valid);
}
