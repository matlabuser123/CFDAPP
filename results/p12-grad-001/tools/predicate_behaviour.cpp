// P12-GRAD-001 GR6: behaviour of MeshGeometry::boundaryFaceAlignment on deterministic geometries --
// exact Cartesian, translated (small and large, non-dyadic), uniformly scaled, one-ulp perturbed,
// genuinely sheared (m = 1e-5 ... 1e-2) and distorted; 2D and 3D.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"

using namespace cfd;

namespace {

void report(const std::string& label, const mesh::Mesh& m, bool expectAligned) {
  Index aligned = 0;
  Index total = 0;
  Real maxSine = 0.0;
  Real minTolerance = 1e300;
  Real maxTolerance = 0.0;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    ++total;
    const auto a = mesh::MeshGeometry::boundaryFaceAlignment(m, face);
    if (a.aligned) ++aligned;
    maxSine = std::max(maxSine, a.sine);
    minTolerance = std::min(minTolerance, a.tolerance);
    maxTolerance = std::max(maxTolerance, a.tolerance);
  }
  const bool ok = expectAligned ? (aligned == total) : (aligned == 0);
  std::printf("%-46s aligned %5zu / %5zu  max sine %.3e  tolerance [%.3e, %.3e]  expected %-11s %s\n",
              label.c_str(), aligned, total, maxSine, minTolerance, maxTolerance,
              expectAligned ? "all aligned" : "none aligned", ok ? "OK" : "MISMATCH");
}

std::vector<Vector3> cartesian(Index n, Real length, const Vector3& offset) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n) * length) + offset.x,
                          (static_cast<Real>(j) / static_cast<Real>(n) * length) + offset.y, 0.0});
    }
  }
  return v;
}

// Shear the interior rows tangentially so every boundary face's owner line is
// off the normal by about `sine`.
std::vector<Vector3> sheared(Index n, Real sine) {
  std::vector<Vector3> v = cartesian(n, 1.0, Vector3{});
  for (Index j = 1; j < n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v[(j * (n + 1)) + i].x += sine * (1.0 / static_cast<Real>(n)) * (j % 2 == 0 ? 1.0 : -1.0);
    }
  }
  return v;
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-001 GR6: boundaryFaceAlignment behaviour\n");
  report("2D 16x16 exact Cartesian (axis builder)", mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), true);
  report("2D 16x16 exact Cartesian (shoelace)",
         mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesian(16, 1.0, Vector3{})), true);
  report("2D 16x16 translated (0.005, 0.0025)",
         mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesian(16, 1.0, Vector3{0.005, 0.0025, 0.0})), true);
  report("2D 16x16 translated (1234.5678, 987.6543)",
         mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesian(16, 1.0, Vector3{1234.5678, 987.6543, 0.0})), true);
  report("2D 64x64 translated (0.005, 0.0025)",
         mesh::MeshGeometry::createStructuredQuad2D(64, 64, cartesian(64, 1.0, Vector3{0.005, 0.0025, 0.0})), true);
  report("2D 256x256 translated (0.005, 0.0025)",
         mesh::MeshGeometry::createStructuredQuad2D(256, 256, cartesian(256, 1.0, Vector3{0.005, 0.0025, 0.0})), true);
  report("2D 16x16 scaled L = 1e-3, offset 0.005 L",
         mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesian(16, 1e-3, Vector3{5e-6, 2.5e-6, 0.0})), true);
  report("2D 16x16 scaled L = 1e3, offset 0.005 L",
         mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesian(16, 1e3, Vector3{5.0, 2.5, 0.0})), true);
  {
    // One vertex moved by a single ulp.
    std::vector<Vector3> v = cartesian(16, 1.0, Vector3{});
    v[8 * 17 + 8].x = std::nextafter(v[8 * 17 + 8].x, 1e300);
    report("2D 16x16 one interior vertex moved by 1 ulp",
           mesh::MeshGeometry::createStructuredQuad2D(16, 16, v), true);
  }
  {
    std::vector<Vector3> v = cartesian(16, 1.0, Vector3{});
    v[0].x = std::nextafter(v[0].x, 1e300);  // a corner boundary vertex
    report("2D 16x16 one boundary vertex moved by 1 ulp",
           mesh::MeshGeometry::createStructuredQuad2D(16, 16, v), true);
  }
  for (const Real sine : {1e-5, 1e-4, 1e-3, 1e-2}) {
    char label[96];
    std::snprintf(label, sizeof(label), "2D 16x16 genuinely sheared, m ~ %.0e", sine);
    report(label, mesh::MeshGeometry::createStructuredQuad2D(16, 16, sheared(16, 0.5 * sine)), false);
  }
  {
    std::vector<Vector3> v;
    for (Index j = 0; j <= 16; ++j) {
      for (Index i = 0; i <= 16; ++i) {
        const Real x = static_cast<Real>(i) / 16.0, y = static_cast<Real>(j) / 16.0, pi = constants::pi;
        v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                            y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
      }
    }
    report("2D 16x16 distorted Q16 (m up to 1.8e-1)",
           mesh::MeshGeometry::createStructuredQuad2D(16, 16, v), false);
  }
  report("3D 8^3 exact Cartesian", mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0), true);
  for (const Real offset : {0.005, 1234.5678}) {
    mesh::Mesh moved = mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
    mesh::MeshMotion motion(moved, std::make_shared<mesh::AffineMotion>(
                                        mesh::AffineMotion::Matrix{}, Vector3{},
                                        Vector3{offset, offset * 0.5, offset * 0.25}));
    (void)motion.advance(1.0);
    char label[96];
    std::snprintf(label, sizeof(label), "3D 8^3 moved by (%g, %g, %g)", offset, offset * 0.5, offset * 0.25);
    report(label, moved, true);
  }
  {
    // 3D sinusoidally deformed: genuinely non-orthogonal boundary cells.
    mesh::Mesh deformed = mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
    mesh::MeshMotion motion(deformed, std::make_shared<mesh::SinusoidalMotion>(
                                          Vector3{0, 0, 0}, Vector3{1, 1, 1}, Vector3{0.05, 0.025, -0.0375},
                                          constants::twoPi / 0.4));
    (void)motion.advance(0.1);
    report("3D 8^3 sinusoidally deformed (genuine)", deformed, false);
  }
  return 0;
}
