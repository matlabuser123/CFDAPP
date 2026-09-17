#pragma once

// P12-MESH-007: the configurations of results/p12-mesh-007/acceptance_gate.md
// (meshes, motions, round-off bounds), shared by the MESH-007 test files so
// every test uses exactly the frozen definitions.

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector3.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/mesh/MultiBlockSpec.hpp"

namespace m7 {

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::mesh::AffineMotion;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using Motion = std::shared_ptr<const cfd::mesh::PrescribedMotion>;
using Matrix = AffineMotion::Matrix;

// --- Definitions ----------------------------------------------------------------------
inline constexpr Real kEps = 2.220446049250313e-16;
inline constexpr Real kDt = 0.02;  // geometry runs
inline constexpr int kSteps = 20;
inline constexpr Real kOmega = cfd::constants::twoPi / 0.4;
inline constexpr Real kAmplitude = 0.05;

// --- Meshes -----------------------------------------------------------------------------
inline Mesh c16() { return MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0); }

inline Mesh g16() {
  const cfd::mesh::AxisGrading grading{cfd::mesh::GradingType::Geometric, 1.2,
                                       cfd::mesh::GradingCluster::Both};
  return MeshGeometry::createGraded2D(16, 16, 1.0, 1.0, grading, grading);
}

// Structured quad: X + 0.03 sin(pi X) sin(2 pi Y) e_x + 0.03 sin(2 pi X) sin(pi Y) e_y.
inline Mesh q16() {
  std::vector<Vector3> vertices;
  vertices.reserve(17 * 17);
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      const Real pi = cfd::constants::pi;
      vertices.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                                 y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(16, 16, vertices);
}

// Two blocks [0,0.5]x[0,1] and [0.5,1]x[0,1], 8x16 each, aligned interface.
inline Mesh mb2() {
  using cfd::mesh::BlockSide;
  const auto grid = [](Real x0, Real x1) {
    std::vector<Vector3> vertices;
    for (Index j = 0; j <= 16; ++j) {
      for (Index i = 0; i <= 8; ++i) {
        vertices.push_back(Vector3{x0 + ((x1 - x0) * (static_cast<Real>(i) / 8.0)),
                                   static_cast<Real>(j) / 16.0, 0.0});
      }
    }
    return vertices;
  };
  cfd::mesh::MultiBlockSpec spec;
  spec.blocks.push_back({"a", 8, 16, grid(0.0, 0.5)});
  spec.blocks.push_back({"b", 8, 16, grid(0.5, 1.0)});
  spec.interfaces.push_back({{0, BlockSide::Right}, {1, BlockSide::Left}, false});
  spec.patches.push_back({"left", {{0, BlockSide::Left}}});
  spec.patches.push_back({"right", {{1, BlockSide::Right}}});
  spec.patches.push_back({"bottom", {{0, BlockSide::Bottom}, {1, BlockSide::Bottom}}});
  spec.patches.push_back({"top", {{0, BlockSide::Top}, {1, BlockSide::Top}}});
  return MeshGeometry::createMultiBlock2D(spec);
}

inline Mesh p32() { return MeshGeometry::createCartesian2D(32, 8, 2.0, 1.0); }
inline Mesh h8() { return MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0); }

// --- Motions ----------------------------------------------------------------------------
struct AffineSpec {
  Matrix rate{};
  Vector3 center{};
  Vector3 velocity{};
};

inline Matrix zeroMatrix() { return Matrix{}; }

inline AffineSpec tr2Spec() { return {zeroMatrix(), {0.5, 0.5, 0.0}, {0.3, -0.2, 0.0}}; }
inline AffineSpec ex2Spec() {
  Matrix g{};
  g[0][0] = 0.5;
  g[1][1] = 0.5;
  return {g, {0.5, 0.5, 0.0}, {}};
}
inline AffineSpec sh2Spec() {
  Matrix g{};
  g[0][1] = 1.0;
  return {g, {0.5, 0.5, 0.0}, {}};
}
inline AffineSpec ps2Spec() {
  Matrix g{};
  g[0][0] = -0.25;
  return {g, {}, {}};
}
inline AffineSpec tc2Spec() { return {zeroMatrix(), {}, {0.5, 0.25, 0.0}}; }
inline AffineSpec tccSpec() { return {zeroMatrix(), {}, {0.4, 0.0, 0.0}}; }
inline AffineSpec tr3Spec() { return {zeroMatrix(), {0.5, 0.5, 0.5}, {0.3, -0.2, 0.1}}; }
inline AffineSpec ex3Spec() {
  Matrix g{};
  g[0][0] = 0.5;
  g[1][1] = 0.5;
  g[2][2] = 0.5;
  return {g, {0.5, 0.5, 0.5}, {}};
}
inline AffineSpec sh3Spec() {
  Matrix g{};
  g[0][1] = 0.5;
  g[0][2] = 0.3;
  g[1][2] = 0.4;
  return {g, {0.5, 0.5, 0.5}, {}};
}
// G2.8(c): axial collapse x = X - 2.6 tau (X - 0.5) e_x.
inline AffineSpec collapseSpec() {
  Matrix g{};
  g[0][0] = -2.6;
  return {g, {0.5, 0.0, 0.0}, {}};
}

inline Motion affine(const AffineSpec& s) {
  return std::make_shared<AffineMotion>(s.rate, s.center, s.velocity);
}
inline Motion stationary() { return std::make_shared<cfd::mesh::StationaryMotion>(); }
inline Motion sn2(Real amplitude = kAmplitude) {
  return std::make_shared<cfd::mesh::SinusoidalMotion>(
      Vector3{0.0, 0.0, 0.0}, Vector3{1.0, 1.0, 0.0}, Vector3{amplitude, amplitude, 0.0}, kOmega);
}
inline Motion sn3(Real amplitude = kAmplitude) {
  return std::make_shared<cfd::mesh::SinusoidalMotion>(
      Vector3{0.0, 0.0, 0.0}, Vector3{1.0, 1.0, 1.0},
      Vector3{amplitude, 0.5 * amplitude, -0.75 * amplitude}, kOmega);
}

// A(tau) = I + tau G of an affine motion; det and cofactor (det A * A^-T,
// which maps area vectors).
inline Matrix deformationGradient(const AffineSpec& s, Real tau) {
  Matrix a{};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      a[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
          (i == j ? 1.0 : 0.0) +
          (tau * s.rate[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]);
    }
  }
  return a;
}
inline Real determinant(const Matrix& a) {
  return (a[0][0] * ((a[1][1] * a[2][2]) - (a[1][2] * a[2][1]))) -
         (a[0][1] * ((a[1][0] * a[2][2]) - (a[1][2] * a[2][0]))) +
         (a[0][2] * ((a[1][0] * a[2][1]) - (a[1][1] * a[2][0])));
}
inline Vector3 cofactorTimes(const Matrix& a, const Vector3& v) {
  // cof(A)_ij = the (i, j) cofactor; cof(A) = det(A) A^-T.
  Matrix c{};
  c[0][0] = (a[1][1] * a[2][2]) - (a[1][2] * a[2][1]);
  c[0][1] = -((a[1][0] * a[2][2]) - (a[1][2] * a[2][0]));
  c[0][2] = (a[1][0] * a[2][1]) - (a[1][1] * a[2][0]);
  c[1][0] = -((a[0][1] * a[2][2]) - (a[0][2] * a[2][1]));
  c[1][1] = (a[0][0] * a[2][2]) - (a[0][2] * a[2][0]);
  c[1][2] = -((a[0][0] * a[2][1]) - (a[0][1] * a[2][0]));
  c[2][0] = (a[0][1] * a[1][2]) - (a[0][2] * a[1][1]);
  c[2][1] = -((a[0][0] * a[1][2]) - (a[0][2] * a[1][0]));
  c[2][2] = (a[0][0] * a[1][1]) - (a[0][1] * a[1][0]);
  return Vector3{(c[0][0] * v.x) + (c[0][1] * v.y) + (c[0][2] * v.z),
                 (c[1][0] * v.x) + (c[1][1] * v.y) + (c[1][2] * v.z),
                 (c[2][0] * v.x) + (c[2][1] * v.y) + (c[2][2] * v.z)};
}

// --- Round-off bounds (acceptance_gate.md) ------------------------------------------------
inline Real maxAbsCoordinate(const std::vector<Vector3>& points) {
  Real x = 0.0;
  for (const Vector3& p : points) x = std::max({x, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
  return x;
}

// Longest of the 12 edges of hexahedron `corner` (StructuredTopology corner order).
inline Real hexLongestEdge(const std::vector<Vector3>& v, const std::array<Index, 8>& c) {
  static constexpr int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                        {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  Real longest = 0.0;
  for (const auto& e : kEdges) {
    longest = std::max(longest, magnitude(v[c[static_cast<std::size_t>(e[1])]] -
                                          v[c[static_cast<std::size_t>(e[0])]]));
  }
  return longest;
}

inline Real faceLongestDiagonal(const std::vector<Vector3>& v, const std::array<Index, 4>& f) {
  return std::max(magnitude(v[f[2]] - v[f[0]]), magnitude(v[f[3]] - v[f[1]]));
}

}  // namespace m7
