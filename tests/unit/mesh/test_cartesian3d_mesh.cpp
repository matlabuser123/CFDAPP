// P12-MESH-005 -- the 3D Cartesian hexahedral mesh (MeshGeometry::createCartesian3D):
// topology counts, volumes, area vectors, centroids, connectivity, boundary
// patches, determinism, dimension detection and input validation
// (results/p12-mesh-005/acceptance_gate.md, items A1-A9).
//
// Every expected value is derived here from the box geometry (dx = Lx / nx,
// ...) and the counting formulas -- never read back from the builder.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshFingerprint.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::Vector3;
using cfd::mesh::AxisGrading;
using cfd::mesh::BlockInterfaceSpec;
using cfd::mesh::BlockSide;
using cfd::mesh::BlockSideRef;
using cfd::mesh::BlockSpec;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::mesh::MeshQuality;
using cfd::mesh::MeshQualitySeverity;
using cfd::mesh::MeshQualityStatus;
using cfd::mesh::MultiBlockSpec;

namespace {

// One test mesh of the gate: M1-M6.
struct Box {
  const char* name;
  Index nx;
  Index ny;
  Index nz;
  Real lx;
  Real ly;
  Real lz;
  Vector3 origin;

  [[nodiscard]] Real dx() const { return lx / static_cast<Real>(nx); }
  [[nodiscard]] Real dy() const { return ly / static_cast<Real>(ny); }
  [[nodiscard]] Real dz() const { return lz / static_cast<Real>(nz); }
  [[nodiscard]] Real largest() const { return std::max({lx, ly, lz}); }
  [[nodiscard]] Mesh build() const {
    return MeshGeometry::createCartesian3D(nx, ny, nz, lx, ly, lz, origin);
  }
  // (i, j, k) of cell `id` under the documented numbering (k * ny + j) * nx + i.
  [[nodiscard]] std::array<Index, 3> ijk(Index id) const {
    return {id % nx, (id / nx) % ny, id / (nx * ny)};
  }
};

const std::vector<Box>& boxes() {
  static const std::vector<Box> all = {
      {"M1 1x1x1 unit cube", 1, 1, 1, 1.0, 1.0, 1.0, Vector3{}},
      {"M2 2x1x1", 2, 1, 1, 1.0, 1.0, 1.0, Vector3{}},
      {"M3 2x2x1", 2, 2, 1, 1.0, 1.0, 1.0, Vector3{}},
      {"M4 2x2x2", 2, 2, 2, 1.0, 1.0, 1.0, Vector3{}},
      {"M5 3x4x5 translated cuboid", 3, 4, 5, 1.5, 0.7, 2.3, Vector3{-1.2, 0.4, 3.1}},
      {"M6 8x8x8 unit cube", 8, 8, 8, 1.0, 1.0, 1.0, Vector3{}},
  };
  return all;
}

// Index of the single non-zero component of an axis-aligned vector (0, 1, 2), or -1.
int axisOf(const Vector3& v) {
  const int nonZero = (v.x != 0.0 ? 1 : 0) + (v.y != 0.0 ? 1 : 0) + (v.z != 0.0 ? 1 : 0);
  if (nonZero != 1) return -1;
  return v.x != 0.0 ? 0 : (v.y != 0.0 ? 1 : 2);
}

Real component(const Vector3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

Vector3 cellOutward(const Face& face, Index cellId) {
  return face.owner() == cellId ? face.areaVector() : face.areaVector() * -1.0;
}

}  // namespace

// --- A1: counts, from the independent plane-counting formulas ---------------
TEST(Cartesian3DMeshTest, TopologyCountsMatchIndependentFormulas) {
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = b.build();
    const Index nx = b.nx;
    const Index ny = b.ny;
    const Index nz = b.nz;
    // Faces: nx+1 planes of ny*nz x-faces, ny+1 planes of nx*nz y-faces, nz+1
    // planes of nx*ny z-faces; the two outer planes of each axis are boundary.
    const Index faces = ((nx + 1) * ny * nz) + (nx * (ny + 1) * nz) + (nx * ny * (nz + 1));
    const Index interior = ((nx - 1) * ny * nz) + (nx * (ny - 1) * nz) + (nx * ny * (nz - 1));
    const Index boundary = 2 * ((ny * nz) + (nx * nz) + (nx * ny));
    ASSERT_EQ(faces, interior + boundary);  // the formulas agree with each other

    EXPECT_EQ(mesh.numberOfCells(), nx * ny * nz);
    EXPECT_EQ(mesh.numberOfFaces(), faces);
    Index counted = 0;
    for (const auto& face : mesh.faces()) counted += face.isBoundary() ? 0 : 1;
    EXPECT_EQ(counted, interior);
    EXPECT_EQ(mesh.numberOfFaces() - counted, boundary);
    ASSERT_NE(mesh.structuredGrid(), nullptr);
    EXPECT_EQ(mesh.structuredGrid()->vertices.size(), (nx + 1) * (ny + 1) * (nz + 1));
    EXPECT_EQ(mesh.structuredGrid()->nz, nz);
    EXPECT_TRUE(mesh.structuredGrid()->isThreeDimensional());
    // Euler characteristic of a (topological) ball: V - E + F - C = 1, with
    // E = the grid edges -- an independent check of the vertex/face/cell counts.
    const Index edges =
        (nx * (ny + 1) * (nz + 1)) + ((nx + 1) * ny * (nz + 1)) + ((nx + 1) * (ny + 1) * nz);
    EXPECT_EQ(static_cast<long long>(mesh.structuredGrid()->vertices.size()) -
                  static_cast<long long>(edges) + static_cast<long long>(faces) -
                  static_cast<long long>(mesh.numberOfCells()),
              1);
  }
}

// --- A2: volumes ---------------------------------------------------------------
TEST(Cartesian3DMeshTest, CellVolumesAreBoxVolumesAndSumToTheDomain) {
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = b.build();
    const Real expected = b.dx() * b.dy() * b.dz();
    Real total = 0.0;
    for (const auto& cell : mesh.cells()) {
      ASSERT_TRUE(std::isfinite(cell.volume()));
      ASSERT_GT(cell.volume(), 0.0);
      EXPECT_LE(std::abs(cell.volume() - expected), 1e-14 * expected) << "cell " << cell.id();
      total += cell.volume();
    }
    const Real domain = b.lx * b.ly * b.lz;
    EXPECT_LE(std::abs(total - domain), 1e-12 * domain);
  }
}

// --- A3: area vectors ------------------------------------------------------------
TEST(Cartesian3DMeshTest, AreaVectorsHaveTheRightMagnitudeDirectionAndClosure) {
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = b.build();
    const std::array<Real, 3> area = {b.dy() * b.dz(), b.dx() * b.dz(), b.dx() * b.dy()};
    std::array<Index, 3> perAxis = {0, 0, 0};
    for (const auto& face : mesh.faces()) {
      const Vector3& sf = face.areaVector();
      const int axis = axisOf(sf);
      ASSERT_GE(axis, 0) << "face " << face.id() << " is not axis-aligned";
      ++perAxis[static_cast<std::size_t>(axis)];
      EXPECT_LE(std::abs(face.area() - area[static_cast<std::size_t>(axis)]),
                1e-14 * area[static_cast<std::size_t>(axis)])
          << "face " << face.id();
      const Vector3& owner = mesh.cell(face.owner()).centroid();
      if (face.isBoundary()) {
        EXPECT_GT(dot(face.centroid() - owner, sf), 0.0) << "boundary face " << face.id();
      } else {
        const Vector3& neighbor = mesh.cell(*face.neighbor()).centroid();
        EXPECT_GT(dot(neighbor - owner, sf), 0.0) << "face " << face.id();
        EXPECT_GT(component(sf, axis), 0.0) << "internal Sf must point toward +axis";
      }
    }
    EXPECT_EQ(perAxis[0], (b.nx + 1) * b.ny * b.nz);
    EXPECT_EQ(perAxis[1], b.nx * (b.ny + 1) * b.nz);
    EXPECT_EQ(perAxis[2], b.nx * b.ny * (b.nz + 1));

    for (const auto& cell : mesh.cells()) {
      Vector3 closure{};
      Real sum = 0.0;
      for (const Index faceId : cell.faceIds()) {
        closure += cellOutward(mesh.face(faceId), cell.id());
        sum += mesh.face(faceId).area();
      }
      EXPECT_LE(magnitude(closure), 1e-13 * sum) << "cell " << cell.id();
    }
  }
}

// --- A4: centroids ---------------------------------------------------------------
TEST(Cartesian3DMeshTest, CentroidsAreBoxAndRectangleCentres) {
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = b.build();
    const Real tol = 1e-14 * b.largest();
    const std::array<Real, 3> d = {b.dx(), b.dy(), b.dz()};
    const std::array<Real, 3> o = {b.origin.x, b.origin.y, b.origin.z};
    for (const auto& cell : mesh.cells()) {
      const auto idx = b.ijk(cell.id());
      for (int a = 0; a < 3; ++a) {
        const auto ua = static_cast<std::size_t>(a);
        const Real low = o[ua] + (static_cast<Real>(idx[ua]) * d[ua]);
        const Real c = component(cell.centroid(), a);
        EXPECT_LE(std::abs(c - (low + (0.5 * d[ua]))), tol) << "cell " << cell.id();
        EXPECT_GT(c, low);
        EXPECT_LT(c, low + d[ua]);
      }
    }
    for (const auto& face : mesh.faces()) {
      const int axis = axisOf(face.areaVector());
      const auto ua = static_cast<std::size_t>(axis);
      const Vector3& owner = mesh.cell(face.owner()).centroid();
      // On the face plane: half a cell from the owner centroid along the normal.
      const Real sign = component(face.areaVector(), axis) > 0.0 ? 1.0 : -1.0;
      EXPECT_LE(std::abs(component(face.centroid(), axis) -
                         (component(owner, axis) + (sign * 0.5 * d[ua]))),
                tol)
          << "face " << face.id();
      // At the rectangle centre: the in-plane coordinates are the owner's.
      for (int t = 0; t < 3; ++t) {
        if (t == axis) continue;
        EXPECT_LE(std::abs(component(face.centroid(), t) - component(owner, t)), tol);
      }
      if (!face.isBoundary()) {
        const Vector3& neighbor = mesh.cell(*face.neighbor()).centroid();
        const Vector3 pn = neighbor - owner;
        EXPECT_TRUE(cross(pn, face.areaVector()) == Vector3{}) << "face " << face.id();
        const Vector3 n = face.areaVector() * (1.0 / face.area());
        const Real t = dot(face.centroid() - owner, n) / dot(pn, n);
        EXPECT_LE(std::abs(t - 0.5), 1e-14) << "face " << face.id();
      }
    }
  }
}

// --- A5: connectivity ------------------------------------------------------------
TEST(Cartesian3DMeshTest, EachCellHasSixFacesInCanonicalOrderAndEachFaceItsOwners) {
  // Local face m has its outward normal along axis m / 2, toward + for odd m:
  // west (-x), east (+x), south (-y), north (+y), bottom (-z), top (+z).
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = b.build();
    const Index cells = mesh.numberOfCells();
    std::vector<std::vector<Index>> incidence(mesh.numberOfFaces());
    for (const auto& cell : mesh.cells()) {
      ASSERT_EQ(cell.faceIds().size(), 6U) << "cell " << cell.id();
      const std::set<Index> distinct(cell.faceIds().begin(), cell.faceIds().end());
      EXPECT_EQ(distinct.size(), 6U);
      for (std::size_t m = 0; m < 6; ++m) {
        const Index faceId = cell.faceIds()[m];
        ASSERT_LT(faceId, mesh.numberOfFaces());
        incidence[faceId].push_back(cell.id());
        const Vector3 outward = cellOutward(mesh.face(faceId), cell.id());
        const int axis = static_cast<int>(m / 2);
        EXPECT_EQ(axisOf(outward), axis) << "cell " << cell.id() << " local face " << m;
        EXPECT_EQ(component(outward, axis) > 0.0, (m % 2) == 1)
            << "cell " << cell.id() << " local face " << m << " is not in canonical order";
      }
    }
    std::set<std::pair<Index, Index>> pairs;
    for (const auto& face : mesh.faces()) {
      ASSERT_LT(face.owner(), cells);
      std::vector<Index> expected{face.owner()};
      if (face.isBoundary()) {
        EXPECT_EQ(incidence[face.id()].size(), 1U);
      } else {
        const Index neighbor = *face.neighbor();
        ASSERT_LT(neighbor, cells);
        EXPECT_NE(neighbor, face.owner());
        EXPECT_LT(face.owner(), neighbor) << "owner must be the lower-index cell";
        EXPECT_TRUE(pairs.emplace(face.owner(), neighbor).second) << "duplicate face";
        EXPECT_EQ(incidence[face.id()].size(), 2U);
        expected.push_back(neighbor);
      }
      std::vector<Index> got = incidence[face.id()];
      std::sort(got.begin(), got.end());
      std::sort(expected.begin(), expected.end());
      EXPECT_EQ(got, expected) << "face " << face.id();
    }
  }
}

// --- A6: determinism and the documented numbering ---------------------------------
TEST(Cartesian3DMeshTest, BuildIsDeterministicAndFollowsTheDocumentedNumbering) {
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh first = b.build();
    const Mesh second = b.build();
    ASSERT_EQ(first.numberOfCells(), second.numberOfCells());
    ASSERT_EQ(first.numberOfFaces(), second.numberOfFaces());
    for (Index c = 0; c < first.numberOfCells(); ++c) {
      EXPECT_TRUE(first.cell(c).centroid() == second.cell(c).centroid());
      EXPECT_EQ(first.cell(c).volume(), second.cell(c).volume());
      EXPECT_EQ(first.cell(c).faceIds(), second.cell(c).faceIds());
    }
    for (Index f = 0; f < first.numberOfFaces(); ++f) {
      EXPECT_EQ(first.face(f).owner(), second.face(f).owner());
      EXPECT_EQ(first.face(f).neighbor(), second.face(f).neighbor());
      EXPECT_TRUE(first.face(f).centroid() == second.face(f).centroid());
      EXPECT_TRUE(first.face(f).areaVector() == second.face(f).areaVector());
    }
    EXPECT_EQ(cfd::mesh::computeMeshFingerprint(first), cfd::mesh::computeMeshFingerprint(second));

    // x-faces (k, j, i = 0..nx), then y-faces (k, j = 0..ny, i), then z-faces.
    const Real tol = 1e-14 * b.largest();
    const Index xFaces = (b.nx + 1) * b.ny * b.nz;
    const Index yFaces = b.nx * (b.ny + 1) * b.nz;
    for (Index k = 0; k < b.nz; ++k) {
      for (Index j = 0; j < b.ny; ++j) {
        for (Index i = 0; i <= b.nx; ++i) {
          const Face& face = first.face((((k * b.ny) + j) * (b.nx + 1)) + i);
          EXPECT_EQ(axisOf(face.areaVector()), 0);
          EXPECT_LE(std::abs(face.centroid().x - (b.origin.x + (static_cast<Real>(i) * b.dx()))),
                    tol);
        }
      }
    }
    for (Index k = 0; k < b.nz; ++k) {
      for (Index j = 0; j <= b.ny; ++j) {
        for (Index i = 0; i < b.nx; ++i) {
          const Face& face = first.face(xFaces + (((k * (b.ny + 1)) + j) * b.nx) + i);
          EXPECT_EQ(axisOf(face.areaVector()), 1);
          EXPECT_LE(std::abs(face.centroid().y - (b.origin.y + (static_cast<Real>(j) * b.dy()))),
                    tol);
        }
      }
    }
    for (Index k = 0; k <= b.nz; ++k) {
      for (Index j = 0; j < b.ny; ++j) {
        for (Index i = 0; i < b.nx; ++i) {
          const Face& face = first.face(xFaces + yFaces + (((k * b.ny) + j) * b.nx) + i);
          EXPECT_EQ(axisOf(face.areaVector()), 2);
          EXPECT_LE(std::abs(face.centroid().z - (b.origin.z + (static_cast<Real>(k) * b.dz()))),
                    tol);
        }
      }
    }
    // The vertex grid: vertex(i, j, k) = origin + (i dx, j dy, k dz).
    const auto* grid = first.structuredGrid();
    for (Index k = 0; k <= b.nz; ++k) {
      for (Index j = 0; j <= b.ny; ++j) {
        for (Index i = 0; i <= b.nx; ++i) {
          const Vector3& v = grid->vertex(i, j, k);
          EXPECT_LE(std::abs(v.x - (b.origin.x + (static_cast<Real>(i) * b.dx()))), tol);
          EXPECT_LE(std::abs(v.y - (b.origin.y + (static_cast<Real>(j) * b.dy()))), tol);
          EXPECT_LE(std::abs(v.z - (b.origin.z + (static_cast<Real>(k) * b.dz()))), tol);
        }
      }
    }
  }
}

// --- A7: boundary patches and the quality report ---------------------------------
TEST(Cartesian3DMeshTest, SixPatchesPartitionTheBoundaryWithOutwardNormals) {
  const std::array<const char*, 6> names = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = b.build();
    ASSERT_EQ(mesh.boundaryPatches().size(), 6U);
    const std::array<Index, 6> counts = {b.ny * b.nz, b.ny * b.nz, b.nx * b.nz,
                                         b.nx * b.nz, b.nx * b.ny, b.nx * b.ny};
    const std::array<Real, 6> sideArea = {b.ly * b.lz, b.ly * b.lz, b.lx * b.lz,
                                          b.lx * b.lz, b.lx * b.ly, b.lx * b.ly};
    const std::array<Real, 3> o = {b.origin.x, b.origin.y, b.origin.z};
    const std::array<Real, 3> l = {b.lx, b.ly, b.lz};
    const Real tol = 1e-14 * b.largest();
    std::vector<int> membership(mesh.numberOfFaces(), 0);
    for (std::size_t p = 0; p < 6; ++p) {
      const auto& patch = mesh.boundaryPatches()[p];
      EXPECT_EQ(patch.name(), names[p]);
      EXPECT_EQ(patch.faceIds().size(), counts[p]);
      const int axis = static_cast<int>(p / 2);
      const bool high = (p % 2) == 1;
      const Real plane = o[p / 2] + (high ? l[p / 2] : 0.0);
      Real total = 0.0;
      for (const Index faceId : patch.faceIds()) {
        const Face& face = mesh.face(faceId);
        ++membership[faceId];
        EXPECT_TRUE(face.isBoundary());
        EXPECT_EQ(axisOf(face.areaVector()), axis);
        EXPECT_EQ(component(face.areaVector(), axis) > 0.0, high) << "outward normal";
        EXPECT_LE(std::abs(component(face.centroid(), axis) - plane), tol);
        total += face.area();
      }
      EXPECT_LE(std::abs(total - sideArea[p]), 1e-13 * sideArea[p]);
    }
    for (const auto& face : mesh.faces()) {
      EXPECT_EQ(membership[face.id()], face.isBoundary() ? 1 : 0) << "face " << face.id();
      if (face.isBoundary()) {
        const int axis = axisOf(face.areaVector());
        const std::size_t side = component(face.areaVector(), axis) > 0.0 ? 1 : 0;
        EXPECT_EQ(cfd::boundary::boundaryPatchNameForFace(mesh, face.id()),
                  names[(2 * static_cast<std::size_t>(axis)) + side]);
      }
    }

    const auto report = MeshQuality::evaluate(mesh);
    EXPECT_EQ(report.status, MeshQualityStatus::Valid) << report.summaryLine();
    for (const auto& issue : report.issues) {
      EXPECT_EQ(issue.severity, MeshQualitySeverity::Info) << issue.message;
    }
    EXPECT_EQ(report.degenerateCells, 0U);
    EXPECT_EQ(report.invalidFaces, 0U);
    EXPECT_EQ(report.connectedComponents, 1U);
    if (report.nonOrthogonality.count > 0) {
      EXPECT_EQ(report.nonOrthogonality.maximum, 0.0);
      EXPECT_EQ(report.skewness.maximum, 0.0);
      EXPECT_EQ(report.expansionRatio.maximum, 1.0);
    }
    const Real aspect = std::max({b.dx(), b.dy(), b.dz()}) / std::min({b.dx(), b.dy(), b.dz()});
    EXPECT_LE(std::abs(report.aspectRatio.maximum - aspect), 1e-13 * aspect);
    EXPECT_LE(std::abs(report.minimumVolume - (b.dx() * b.dy() * b.dz())),
              1e-14 * b.dx() * b.dy() * b.dz());
  }
}

// --- A8: dimension -------------------------------------------------------------------
TEST(Cartesian3DMeshTest, DimensionIsThreeFor3DMeshesAndTwoForEvery2DBuilder) {
  for (const Box& b : boxes()) EXPECT_EQ(b.build().dimension(), 3) << b.name;

  EXPECT_EQ(MeshGeometry::createCartesian2D(4, 3, 1.0, 2.0).dimension(), 2);
  AxisGrading graded;
  graded.type = cfd::mesh::GradingType::Geometric;
  graded.ratio = 1.2;
  EXPECT_EQ(MeshGeometry::createGraded2D(6, 4, 2.0, 1.0, graded, AxisGrading{}).dimension(), 2);
  std::vector<Vector2> lattice;
  for (Index j = 0; j <= 3; ++j) {
    for (Index i = 0; i <= 4; ++i) {
      lattice.push_back(
          Vector2{static_cast<Real>(i) + (0.1 * static_cast<Real>(j)), static_cast<Real>(j)});
    }
  }
  EXPECT_EQ(MeshGeometry::createStructuredQuad2D(4, 3, lattice).dimension(), 2);
  MultiBlockSpec spec;
  const auto block = [](const std::string& name, Real x0) {
    BlockSpec s{name, 2, 2, {}};
    for (Index j = 0; j <= 2; ++j) {
      for (Index i = 0; i <= 2; ++i) {
        s.vertices.push_back(
            Vector2{x0 + (0.5 * static_cast<Real>(i)), 0.5 * static_cast<Real>(j)});
      }
    }
    return s;
  };
  spec.blocks = {block("a", 0.0), block("b", 1.0)};
  spec.interfaces = {BlockInterfaceSpec{BlockSideRef{0, BlockSide::Right},
                                        BlockSideRef{1, BlockSide::Left}, false}};
  spec.patches = {{"left", {BlockSideRef{0, BlockSide::Left}}},
                  {"right", {BlockSideRef{1, BlockSide::Right}}},
                  {"walls",
                   {BlockSideRef{0, BlockSide::Bottom}, BlockSideRef{1, BlockSide::Bottom},
                    BlockSideRef{0, BlockSide::Top}, BlockSideRef{1, BlockSide::Top}}}};
  EXPECT_EQ(MeshGeometry::createMultiBlock2D(spec).dimension(), 2);
}

// A 2D vertex grid cannot be attached to a 3D mesh (or the reverse), and a
// 3D grid needs (nx+1)(ny+1)(nz+1) vertices.
TEST(Cartesian3DMeshTest, StructuredGridMustMatchTheMeshDimension) {
  const Mesh mesh3 = MeshGeometry::createCartesian3D(2, 1, 1, 1.0, 1.0, 1.0);
  cfd::mesh::StructuredGrid flat{2, 1, std::vector<Vector2>(6), {}};
  EXPECT_THROW((Mesh(mesh3.cells(), mesh3.faces(), mesh3.boundaryPatches(), flat)),
               InvalidArgumentError);
  cfd::mesh::StructuredGrid shortGrid{2, 1, std::vector<Vector2>(11), {}, 1};
  EXPECT_THROW((Mesh(mesh3.cells(), mesh3.faces(), mesh3.boundaryPatches(), shortGrid)),
               InvalidArgumentError);
  const Mesh mesh2 = MeshGeometry::createCartesian2D(2, 1, 1.0, 1.0);
  cfd::mesh::StructuredGrid deep{2, 1, std::vector<Vector2>(12), {}, 1};
  EXPECT_THROW((Mesh(mesh2.cells(), mesh2.faces(), mesh2.boundaryPatches(), deep)),
               InvalidArgumentError);
}

// --- A9: invalid input ------------------------------------------------------------
TEST(Cartesian3DMeshTest, InvalidDimensionsAndExtentsAreRejected) {
  const Real inf = std::numeric_limits<Real>::infinity();
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW((void)MeshGeometry::createCartesian3D(0, 1, 1, 1.0, 1.0, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createCartesian3D(1, 0, 1, 1.0, 1.0, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createCartesian3D(1, 1, 0, 1.0, 1.0, 1.0), InvalidArgumentError);
  for (const Real bad : {0.0, -1.0, inf, nan}) {
    EXPECT_THROW((void)MeshGeometry::createCartesian3D(1, 1, 1, bad, 1.0, 1.0),
                 InvalidArgumentError);
    EXPECT_THROW((void)MeshGeometry::createCartesian3D(1, 1, 1, 1.0, bad, 1.0),
                 InvalidArgumentError);
    EXPECT_THROW((void)MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, bad),
                 InvalidArgumentError);
  }
  EXPECT_THROW(
      (void)MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, 1.0, Vector3{0.0, nan, 0.0}),
      InvalidArgumentError);
  EXPECT_THROW(
      (void)MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, 1.0, Vector3{0.0, 0.0, inf}),
      InvalidArgumentError);
}

// The fingerprint of a 3D mesh sees z (two meshes differing only in the z
// origin differ), while a 2D mesh's fingerprint is unchanged by P12-MESH-005
// (golden values from the pre-P12-MESH-005 build: results/p12-mesh-005,
// logs/02_2d_bit_identity_probe.log).
TEST(Cartesian3DMeshTest, FingerprintSeesZIn3DAndKeeps2DValues) {
  const Mesh a = MeshGeometry::createCartesian3D(2, 2, 2, 1.0, 1.0, 1.0, Vector3{0.0, 0.0, 0.0});
  const Mesh b = MeshGeometry::createCartesian3D(2, 2, 2, 1.0, 1.0, 1.0, Vector3{0.0, 0.0, 0.5});
  EXPECT_NE(cfd::mesh::computeMeshFingerprint(a), cfd::mesh::computeMeshFingerprint(b));
  EXPECT_EQ(cfd::mesh::computeMeshFingerprint(MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0)),
            "f6fc7c6e993bc9ac");
  EXPECT_EQ(cfd::mesh::computeMeshFingerprint(MeshGeometry::createCartesian2D(13, 7, 2.1, 0.9)),
            "a267b75d368102a4");
}

// Every 2D-only component refuses a 3D mesh explicitly (acceptance gate C5
// for the mesh-level ones; the solver-level ones are in their own suites).
TEST(Cartesian3DMeshTest, RequireTwoDimensionalNamesTheComponent) {
  const Mesh mesh3 = MeshGeometry::createCartesian3D(2, 2, 2, 1.0, 1.0, 1.0);
  try {
    cfd::mesh::requireTwoDimensional(mesh3, "SomeSolver");
    FAIL() << "expected InvalidArgumentError";
  } catch (const InvalidArgumentError& e) {
    EXPECT_NE(std::string(e.what()).find("SomeSolver"), std::string::npos);
  }
  EXPECT_NO_THROW(
      cfd::mesh::requireTwoDimensional(MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0), "x"));
}
