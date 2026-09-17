// P12-MESH-002: MeshGeometry::createRectilinear2D / createGraded2D -- the
// graded structured mesh built by the one structured topology builder the
// Cartesian mesh also uses. Verification: the uniform grading reproduces
// createCartesian2D bit for bit; graded meshes have exactly the requested
// cell sizes, are exactly orthogonal, closed and valid; spacing metrics
// (min/max width, adjacent ratios, closure) are reported.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshFingerprint.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::mesh::AxisGrading;
using cfd::mesh::GradingCluster;
using cfd::mesh::GradingType;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::mesh::MeshQuality;

namespace {

AxisGrading geometric(Real ratio, GradingCluster cluster) {
  return AxisGrading{GradingType::Geometric, ratio, cluster};
}

void expectBitIdentical(const Mesh& a, const Mesh& b) {
  ASSERT_EQ(a.numberOfCells(), b.numberOfCells());
  ASSERT_EQ(a.numberOfFaces(), b.numberOfFaces());
  for (Index c = 0; c < a.numberOfCells(); ++c) {
    EXPECT_EQ(a.cell(c).centroid().x, b.cell(c).centroid().x);
    EXPECT_EQ(a.cell(c).centroid().y, b.cell(c).centroid().y);
    EXPECT_EQ(a.cell(c).volume(), b.cell(c).volume());
    EXPECT_EQ(a.cell(c).faceIds(), b.cell(c).faceIds());
  }
  for (Index f = 0; f < a.numberOfFaces(); ++f) {
    EXPECT_EQ(a.face(f).owner(), b.face(f).owner());
    EXPECT_EQ(a.face(f).neighbor(), b.face(f).neighbor());
    EXPECT_EQ(a.face(f).centroid().x, b.face(f).centroid().x);
    EXPECT_EQ(a.face(f).centroid().y, b.face(f).centroid().y);
    EXPECT_EQ(a.face(f).areaVector().x, b.face(f).areaVector().x);
    EXPECT_EQ(a.face(f).areaVector().y, b.face(f).areaVector().y);
  }
  ASSERT_NE(a.structuredGrid(), nullptr);
  ASSERT_NE(b.structuredGrid(), nullptr);
  EXPECT_EQ(a.structuredGrid()->vertices.size(), b.structuredGrid()->vertices.size());
  for (std::size_t k = 0; k < a.structuredGrid()->vertices.size(); ++k) {
    EXPECT_EQ(a.structuredGrid()->vertices[k].x, b.structuredGrid()->vertices[k].x);
    EXPECT_EQ(a.structuredGrid()->vertices[k].y, b.structuredGrid()->vertices[k].y);
  }
  EXPECT_EQ(cfd::mesh::computeMeshFingerprint(a), cfd::mesh::computeMeshFingerprint(b));
}

// Column widths / row heights of a structured mesh from its vertex grid.
std::vector<Real> widths(const Mesh& mesh, bool xAxis) {
  const auto* grid = mesh.structuredGrid();
  std::vector<Real> w;
  const Index n = xAxis ? grid->nx : grid->ny;
  for (Index k = 0; k < n; ++k) {
    w.push_back(xAxis ? grid->vertex(k + 1, 0).x - grid->vertex(k, 0).x
                      : grid->vertex(0, k + 1).y - grid->vertex(0, k).y);
  }
  return w;
}

}  // namespace

// --- Uniform defaults preserved ------------------------------------------------------

TEST(GradedMesh, UniformGradingIsTheCartesianMeshBitForBit) {
  for (const auto& [nx, ny, lx, ly] : {std::tuple<Index, Index, Real, Real>{1, 1, 1.0, 1.0},
                                       {7, 3, 0.3, 0.1},
                                       {64, 8, 8.0, 1.0},
                                       {48, 24, 4.0, 1.0}}) {
    const Mesh cartesian = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
    expectBitIdentical(MeshGeometry::createGraded2D(nx, ny, lx, ly, AxisGrading{}, AxisGrading{}),
                       cartesian);
    // ratio exactly 1 is uniform too
    expectBitIdentical(
        MeshGeometry::createGraded2D(nx, ny, lx, ly, geometric(1.0, GradingCluster::Start),
                                     geometric(1.0, GradingCluster::Both)),
        cartesian);
  }
}

// --- Graded geometry -------------------------------------------------------------------

// y-only, x-only and x+y: every cell is exactly the requested rectangle, the
// mesh is exactly orthogonal/unskewed, closed, valid, and tiles the domain.
TEST(GradedMesh, GradedMeshesHaveTheRequestedCellsAndAreValid) {
  struct Variant {
    const char* name;
    AxisGrading x;
    AxisGrading y;
  };
  const Real lx = 4.0;
  const Real ly = 1.0;
  const Index nx = 24;
  const Index ny = 16;
  for (const Variant& v :
       {Variant{"y both 1.2", AxisGrading{}, geometric(1.2, GradingCluster::Both)},
        Variant{"y bottom 1.15", AxisGrading{}, geometric(1.15, GradingCluster::Start)},
        Variant{"y top 1.15", AxisGrading{}, geometric(1.15, GradingCluster::End)},
        Variant{"x left 1.1", geometric(1.1, GradingCluster::Start), AxisGrading{}},
        Variant{"x right 1.1", geometric(1.1, GradingCluster::End), AxisGrading{}},
        Variant{"x both 1.1 + y both 1.2", geometric(1.1, GradingCluster::Both),
                geometric(1.2, GradingCluster::Both)}}) {
    const Mesh mesh = MeshGeometry::createGraded2D(nx, ny, lx, ly, v.x, v.y);
    // Expected cells from the axis spacings (a uniform axis keeps the
    // Cartesian arithmetic, width = L / n; a graded one the graded nodes).
    const auto xs = cfd::mesh::AxisSpacing::graded(nx, lx, v.x);
    const auto ys = cfd::mesh::AxisSpacing::graded(ny, ly, v.y);
    const auto xNodes = cfd::mesh::gradedNodeCoordinates(nx, lx, v.x);
    const auto yNodes = cfd::mesh::gradedNodeCoordinates(ny, ly, v.y);
    Real area = 0.0;
    Real minArea = 1e300;
    for (Index j = 0; j < ny; ++j) {
      for (Index i = 0; i < nx; ++i) {
        const auto& c = mesh.cell((j * nx) + i);
        EXPECT_EQ(c.centroid().x, xs.center(i)) << v.name;
        EXPECT_EQ(c.centroid().y, ys.center(j)) << v.name;
        EXPECT_EQ(c.volume(), xs.width(i) * ys.width(j)) << v.name;
        if (!xs.isUniform()) {
          EXPECT_EQ(xs.width(i), xNodes[i + 1] - xNodes[i]) << v.name;
        }
        if (!ys.isUniform()) {
          EXPECT_EQ(ys.width(j), yNodes[j + 1] - yNodes[j]) << v.name;
        }
        area += c.volume();
        minArea = std::min(minArea, c.volume());
      }
    }
    EXPECT_NEAR(area, lx * ly, 1e-13 * lx * ly) << v.name;
    EXPECT_GT(minArea, 0.0) << v.name;
    const auto report = MeshQuality::evaluate(mesh);
    EXPECT_TRUE(report.valid) << v.name;
    EXPECT_TRUE(report.problems.empty()) << v.name;
    EXPECT_EQ(report.maxNonOrthogonalityDegrees, 0.0) << v.name;  // exactly orthogonal
    EXPECT_EQ(report.maxSkewness, 0.0) << v.name;

    // Spacing metrics (reported for the evidence).
    for (const bool xAxis : {true, false}) {
      const auto w = widths(mesh, xAxis);
      const AxisGrading& g = xAxis ? v.x : v.y;
      Real lo = w[0];
      Real hi = w[0];
      Real sum = 0.0;
      Real ratioLo = 1e300;
      Real ratioHi = 0.0;
      for (std::size_t k = 0; k < w.size(); ++k) {
        lo = std::min(lo, w[k]);
        hi = std::max(hi, w[k]);
        sum += w[k];
        if (k + 1 < w.size()) {
          const Real ratio = std::max(w[k + 1] / w[k], w[k] / w[k + 1]);
          ratioLo = std::min(ratioLo, ratio);
          ratioHi = std::max(ratioHi, ratio);
        }
      }
      const Real length = xAxis ? lx : ly;
      EXPECT_NEAR(sum, length, 1e-13 * length) << v.name;
      if (g.type == GradingType::Geometric) {
        EXPECT_NEAR(ratioHi, g.ratio, 1e-9) << v.name;  // every adjacent ratio is r (or 1 at the
                                                        // centre of a two-sided distribution)
      } else {
        EXPECT_NEAR(ratioHi, 1.0, 1e-12) << v.name;
      }
      std::printf(
          "%-26s %s: width min %.6f max %.6f (max/min %.3f), requested ratio %.3f, "
          "adjacent ratios %.6f..%.6f, closure error %.1e\n",
          v.name, xAxis ? "x" : "y", lo, hi, hi / lo,
          g.type == GradingType::Geometric ? g.ratio : 1.0, ratioLo, ratioHi,
          std::abs(sum - length));
    }
  }
}

// The graded vertex grid (exported to VTK) is exactly the graded nodes.
TEST(GradedMesh, VertexGridIsTheGradedNodes) {
  const AxisGrading gy = geometric(1.2, GradingCluster::Both);
  const Mesh mesh = MeshGeometry::createGraded2D(6, 8, 2.0, 1.0, AxisGrading{}, gy);
  const auto ys = cfd::mesh::gradedNodeCoordinates(8, 1.0, gy);
  for (Index j = 0; j <= 8; ++j) {
    for (Index i = 0; i <= 6; ++i) {
      EXPECT_EQ(mesh.structuredGrid()->vertex(i, j).y, ys[j]);
      EXPECT_EQ(mesh.structuredGrid()->vertex(i, j).x, static_cast<Real>(i) * (2.0 / 6.0));
    }
  }
}

TEST(GradedMesh, ConstructionIsDeterministic) {
  const auto build = [] {
    return MeshGeometry::createGraded2D(20, 12, 3.0, 1.0, geometric(1.07, GradingCluster::End),
                                        geometric(1.2, GradingCluster::Both));
  };
  expectBitIdentical(build(), build());
  EXPECT_NE(cfd::mesh::computeMeshFingerprint(build()),
            cfd::mesh::computeMeshFingerprint(MeshGeometry::createCartesian2D(20, 12, 3.0, 1.0)));
}

TEST(GradedMesh, InvalidGradingNeverBuildsAMesh) {
  EXPECT_THROW((void)MeshGeometry::createGraded2D(8, 400, 1.0, 1.0, AxisGrading{},
                                                  geometric(10.0, GradingCluster::Start)),
               cfd::InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createGraded2D(
                   8, 8, 1.0, 1.0, geometric(0.5, GradingCluster::Both), AxisGrading{}),
               cfd::InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createGraded2D(8, 8, 0.0, 1.0, AxisGrading{}, AxisGrading{}),
               cfd::InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createGraded2D(0, 8, 1.0, 1.0, AxisGrading{}, AxisGrading{}),
               cfd::InvalidArgumentError);
}
