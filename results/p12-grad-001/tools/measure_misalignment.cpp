// P12-GRAD-001, Part A: measure the ROUND-OFF misalignment of boundary faces that the Green-Gauss
// paired-boundary predicate tests, so its tolerance can be derived from floating-point scaling and mesh
// geometry (never from the G6.3 result). Pre-fix measurement; no Gradient.cpp change yet.
//
// Quantity: for every boundary face, the normalized misalignment
//     m = |d x S_f| / (|d| |S_f|) = sin(angle between d = x_f - x_P and S_f),
// dimensionless and invariant under a uniform rescaling of the mesh. The existing predicate demands
// m == 0 exactly.
//
// Meshes: the same Cartesian geometry built two ways -- createCartesian2D (axis formulas, exact) and
// createStructuredQuad2D (shoelace geometry of the same vertices, as any moved/translated mesh has) --
// at several resolutions, translations and scales; and the 3D Cartesian mesh moved by the MESH-007
// kernel. Also the genuinely non-orthogonal Q16 mesh, for separation.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"

using namespace cfd;

namespace {

struct Misalignment {
  Real maxSine{0.0};
  Real maxRatioToEps{0.0};  // maxSine / (eps * (X / h)^3)
  Index exactlyZero{0};
  Index faces{0};
  // The paired treatment's SECOND geometric predicate: the boundary face and
  // its opposite interior face must have the same area to 1e-12 relative.
  Real maxAreaMismatch{0.0};       // |A_b - A_o| / A_b
  Real maxAreaRatioToEps{0.0};     // that, over eps (X / h)^2
  Index areaMismatchOverFixed{0};  // faces exceeding the existing fixed 1e-12
};

Misalignment measure(const mesh::Mesh& m) {
  const Real eps = 2.220446049250313e-16;
  Misalignment result;
  Real maxCoordinate = 0.0;
  for (const auto& cell : m.cells()) {
    maxCoordinate = std::max({maxCoordinate, std::abs(cell.centroid().x), std::abs(cell.centroid().y),
                              std::abs(cell.centroid().z)});
  }
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    ++result.faces;
    const Vector3 d = face.centroid() - m.cell(face.owner()).centroid();
    const Vector3 s = face.areaVector();
    const Real sine = magnitude(cross(d, s)) / (magnitude(d) * magnitude(s));
    if (cross(d, s) == Vector3{}) ++result.exactlyZero;
    result.maxSine = std::max(result.maxSine, sine);
    const Real h = 2.0 * magnitude(d);
    const Real ratio = maxCoordinate > 0.0 ? sine / (eps * std::pow(maxCoordinate / h, 3.0)) : 0.0;
    result.maxRatioToEps = std::max(result.maxRatioToEps, ratio);
    const auto opposite =
        mesh::MeshGeometry::oppositeInteriorFace(m, m.cell(face.owner()), face);
    if (opposite.has_value()) {
      const Real mismatch = std::abs(face.area() - m.face(*opposite).area()) / face.area();
      result.maxAreaMismatch = std::max(result.maxAreaMismatch, mismatch);
      if (mismatch > 1e-12) ++result.areaMismatchOverFixed;
      const Real areaRatio = maxCoordinate > 0.0 ? mismatch / (eps * std::pow(maxCoordinate / h, 2.0)) : 0.0;
      result.maxAreaRatioToEps = std::max(result.maxAreaRatioToEps, areaRatio);
    }
  }
  return result;
}

void report(const char* label, const mesh::Mesh& m) {
  const Misalignment r = measure(m);
  std::printf("%-40s faces %4zu exact-par %4zu max sin %.3e sin/(eps(X/h)^3) %.3e | max area mismatch "
              "%.3e over-1e-12 %4zu ratio/(eps(X/h)^2) %.3e\n",
              label, r.faces, r.exactlyZero, r.maxSine, r.maxRatioToEps, r.maxAreaMismatch,
              r.areaMismatchOverFixed, r.maxAreaRatioToEps);
}

std::vector<Vector3> cartesianVertices(Index n, Real length, Real offset) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n) * length) + offset,
                          (static_cast<Real>(j) / static_cast<Real>(n) * length) + offset, 0.0});
    }
  }
  return v;
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-001 Part A: round-off misalignment of boundary faces (pre-fix)\n");
  std::printf("## 2D: the same Cartesian geometry, built by the axis formulas and by the shoelace builder\n");
  for (const Index n : {8u, 16u, 64u, 256u}) {
    char label[96];
    std::snprintf(label, sizeof(label), "createCartesian2D %zux%zu", static_cast<std::size_t>(n),
                  static_cast<std::size_t>(n));
    report(label, mesh::MeshGeometry::createCartesian2D(n, n, 1.0, 1.0));
    for (const Real offset : {0.0, 0.005, 1.0, 100.0}) {
      std::snprintf(label, sizeof(label), "structuredQuad2D %zux%zu, offset %g", static_cast<std::size_t>(n),
                    static_cast<std::size_t>(n), offset);
      report(label, mesh::MeshGeometry::createStructuredQuad2D(n, n, cartesianVertices(n, 1.0, offset)));
    }
  }
  std::printf("## 2D scaling: 16x16, domain length L, offset 0.005 L (scale invariance)\n");
  for (const Real length : {1e-3, 1.0, 1e3}) {
    char label[96];
    std::snprintf(label, sizeof(label), "structuredQuad2D 16x16, L = %g", length);
    report(label, mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesianVertices(16, length, 0.005 * length)));
  }
  std::printf("## 3D: Cartesian mesh moved by the MESH-007 kernel (rigid translation)\n");
  for (const Index n : {8u, 16u, 32u}) {
    char label[96];
    std::snprintf(label, sizeof(label), "createCartesian3D %zu^3", static_cast<std::size_t>(n));
    mesh::Mesh cube = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
    report(label, cube);
    for (const Real offset : {0.005, 1.0}) {
      mesh::Mesh moved = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
      mesh::MeshMotion motion(moved, std::make_shared<mesh::AffineMotion>(
                                          mesh::AffineMotion::Matrix{}, Vector3{},
                                          Vector3{offset, offset, offset}));
      (void)motion.advance(1.0);
      std::snprintf(label, sizeof(label), "cartesian3D %zu^3 moved by %g", static_cast<std::size_t>(n), offset);
      report(label, moved);
    }
  }
  std::printf("## genuinely non-orthogonal reference (Q16 of the MESH-007 gate)\n");
  {
    std::vector<Vector3> v;
    for (Index j = 0; j <= 16; ++j) {
      for (Index i = 0; i <= 16; ++i) {
        const Real x = static_cast<Real>(i) / 16.0, y = static_cast<Real>(j) / 16.0, pi = constants::pi;
        v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                            y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
      }
    }
    report("Q16 distorted (genuine non-orthogonality)", mesh::MeshGeometry::createStructuredQuad2D(16, 16, v));
  }
  std::printf("## controlled small non-orthogonality: boundary cells sheared by angle theta\n");
  for (const Real theta : {1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-3}) {
    std::vector<Vector3> v = cartesianVertices(16, 1.0, 0.0);
    // Shift interior vertex rows tangentially so the owner centroid moves off the face normal by ~theta.
    for (Index j = 0; j <= 16; ++j) {
      for (Index i = 0; i <= 16; ++i) {
        if (j == 0 || j == 16) continue;
        v[(j * 17) + i].x += theta * (1.0 / 16.0) * static_cast<Real>(j % 2 == 0 ? 1 : -1);
      }
    }
    char label[96];
    std::snprintf(label, sizeof(label), "16x16 sheared by theta = %g", theta);
    report(label, mesh::MeshGeometry::createStructuredQuad2D(16, 16, v));
  }
  return 0;
}
