// P12-DIFF-001: how large is the tangential offset d_t on the boundaries of BOTH failing cases?
// The proposed correction scales with |d_t|, so this bounds its possible effect before any code
// change. Also reports d_n vs |d| (their ratio is what a "straight-line distance defect" would
// need to be) -- but note diff_identity.cpp already proves the production coefficient is
// Gamma |S| / d_n exactly, so |d| never enters the corrected Dirichlet path.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

const Real kPi = std::acos(-1.0);

std::vector<Vector2> poiseuille(Index nx, Index ny, Real distort) {
  const Real L = 8.0, H = 1.0, lambda = 1.0;
  const Real ax = 0.1 * distort, ay = 0.05 * distort;
  std::vector<Vector2> v;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = L * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = H * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / L) * std::sin(2.0 * kPi * eta / H));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / H));
      if (i == 0) x = 0.0;
      if (i == nx) x = L;
      if (j == 0) y = 0.0;
      if (j == ny) y = H;
      v.push_back(Vector2{x, y});
    }
  }
  return v;
}

// MESH-003's annular geometry as a single block over the full 270 degrees: the inner/outer curved
// walls are identical to the three-block case's, which is all this geometric measurement needs (no
// interfaces required).
std::vector<Vector2> annular(Index nr, Index nTotal) {
  const Real sweep = 1.5 * kPi;
  std::vector<Vector2> v;
  for (Index j = 0; j <= nTotal; ++j) {
    const Real t = sweep * static_cast<Real>(j) / static_cast<Real>(nTotal);
    for (Index i = 0; i <= nr; ++i) {
      const Real r = 1.0 + (static_cast<Real>(i) / static_cast<Real>(nr));
      v.push_back(Vector2{r * std::cos(t), r * std::sin(t)});
    }
  }
  return v;
}

void report(const char* label, const mesh::Mesh& m) {
  Real worstTangentRatio = 0.0;  // |d_t| / d_n
  Real worstDistanceRatio = 0.0;  // |d| / d_n - 1
  Index faces = 0;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    const Vector3 d = face.centroid() - m.cell(face.owner()).centroid();
    const Vector3 n = face.areaVector() * (1.0 / face.area());
    const Real dn = dot(d, n);
    if (!(dn > 0.0)) continue;
    const Vector3 dt = d - (n * dn);
    worstTangentRatio = std::max(worstTangentRatio, magnitude(dt) / dn);
    worstDistanceRatio = std::max(worstDistanceRatio, (magnitude(d) / dn) - 1.0);
    ++faces;
  }
  std::printf("G   %-40s boundary faces %5zu | max |d_t|/d_n %.4e | max (|d|/d_n - 1) %.4e\n",
              label, static_cast<std::size_t>(faces), worstTangentRatio, worstDistanceRatio);
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-001: boundary tangential offset on both failing cases\n");
  report("Poiseuille 144x18, distortion 0",
         mesh::MeshGeometry::createStructuredQuad2D(144, 18, poiseuille(144, 18, 0.0)));
  report("Poiseuille 144x18, distortion 1 (48 deg)",
         mesh::MeshGeometry::createStructuredQuad2D(144, 18, poiseuille(144, 18, 1.0)));
  report("curved channel 8x60 (1 block)",
         mesh::MeshGeometry::createStructuredQuad2D(8, 60, annular(8, 60)));
  report("curved channel 18x135 (1 block)",
         mesh::MeshGeometry::createStructuredQuad2D(18, 135, annular(18, 135)));
  return 0;
}
