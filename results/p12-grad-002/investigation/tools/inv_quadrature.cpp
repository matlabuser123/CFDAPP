// P12-GRAD-002-INV-001, step 11: CHARACTERIZE ONLY the deformed-3D failure that stopped A1.
// Nothing is fixed here.
//
// Hypothesis under test: for a LINEAR field, one stored face centroid plus one stored area vector
// cannot represent  integral(phi n dS)  over a WARPED (bilinear, non-planar) quadrilateral face,
// so Green-Gauss cannot be exact for a linear field on a deformed 3D mesh however good the
// boundary treatment is.
//
// Reference: integral(phi n dS) = integral over (u,v) in [0,1]^2 of phi(x(u,v)) (x_u x x_v) du dv
// on the bilinear patch x(u,v) = (1-u)(1-v) p0 + u(1-v) p1 + uv p2 + (1-u)v p3. For linear phi the
// integrand is degree <= 2 per variable, so a 4x4 Gauss rule integrates it exactly; 2x2 and 4x4 are
// both reported so the quadrature itself is self-checking.
// Compared against the single-point representation phi(x_face) * S_f that the discretization uses.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"

using namespace cfd;

namespace {

// phi = a . x + b, the field A1's C2 clause requires to be exact.
const Vector3 kA{1.3, -0.9, 0.4};
constexpr Real kB = 0.7;
Real phiOf(const Vector3& x) { return dot(kA, x) + kB; }

Vector3 bilinearPoint(const std::array<Vector3, 4>& p, Real u, Real v) {
  return (p[0] * ((1.0 - u) * (1.0 - v))) + (p[1] * (u * (1.0 - v))) + (p[2] * (u * v)) +
         (p[3] * ((1.0 - u) * v));
}

Vector3 bilinearDu(const std::array<Vector3, 4>& p, Real v) {
  return ((p[1] - p[0]) * (1.0 - v)) + ((p[2] - p[3]) * v);
}

Vector3 bilinearDv(const std::array<Vector3, 4>& p, Real u) {
  return ((p[3] - p[0]) * (1.0 - u)) + ((p[2] - p[1]) * u);
}

// Gauss-Legendre nodes/weights on [0,1].
struct Rule {
  std::vector<Real> node;
  std::vector<Real> weight;
};

Rule gauss(int n) {
  Rule r;
  if (n == 2) {
    const Real a = 0.5 / std::sqrt(3.0);
    r.node = {0.5 - a, 0.5 + a};
    r.weight = {0.5, 0.5};
  } else {
    const Real x1 = std::sqrt((3.0 / 7.0) - ((2.0 / 7.0) * std::sqrt(6.0 / 5.0)));
    const Real x2 = std::sqrt((3.0 / 7.0) + ((2.0 / 7.0) * std::sqrt(6.0 / 5.0)));
    const Real w1 = (18.0 + std::sqrt(30.0)) / 36.0;
    const Real w2 = (18.0 - std::sqrt(30.0)) / 36.0;
    r.node = {0.5 - (0.5 * x2), 0.5 - (0.5 * x1), 0.5 + (0.5 * x1), 0.5 + (0.5 * x2)};
    r.weight = {0.5 * w2, 0.5 * w1, 0.5 * w1, 0.5 * w2};
  }
  return r;
}

// integral(phi n dS) over the bilinear patch, and integral(n dS) as a cross-check on the rule.
std::pair<Vector3, Vector3> integrate(const std::array<Vector3, 4>& p, int order) {
  const Rule r = gauss(order);
  Vector3 phiIntegral{};
  Vector3 areaIntegral{};
  for (std::size_t i = 0; i < r.node.size(); ++i) {
    for (std::size_t j = 0; j < r.node.size(); ++j) {
      const Real u = r.node[i];
      const Real v = r.node[j];
      const Vector3 normal = cross(bilinearDu(p, v), bilinearDv(p, u));
      const Real w = r.weight[i] * r.weight[j];
      phiIntegral = phiIntegral + (normal * (w * phiOf(bilinearPoint(p, u, v))));
      areaIntegral = areaIntegral + (normal * w);
    }
  }
  return {phiIntegral, areaIntegral};
}

void study(Index n, Real amplitude, const char* label) {
  mesh::Mesh m = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  if (amplitude > 0.0) {
    mesh::MeshMotion motion(
        m, std::make_shared<mesh::SinusoidalMotion>(
               Vector3{0, 0, 0}, Vector3{1, 1, 1},
               Vector3{amplitude, 0.5 * amplitude, -0.75 * amplitude}, constants::twoPi / 0.4));
    (void)motion.advance(0.1);
  }
  const auto topology = mesh::MeshGeometry::structuredTopology(m);

  Real worstFaceRelative = 0.0;
  Real worstRuleCheck = 0.0;
  Real worstAreaCheck = 0.0;
  Real worstWarp = 0.0;
  // Per-cell divergence residual: sum_f integral(phi n dS) is exactly V grad(phi) for linear phi,
  // whereas sum_f phi(x_f) S_f is what the discretization computes.
  Real worstCellRelative = 0.0;
  for (const auto& cell : m.cells()) {
    Vector3 exactSum{};
    Vector3 pointSum{};
    for (const Index faceId : cell.faceIds()) {
      const auto& face = m.face(faceId);
      const auto& fv = topology.faces[faceId];
      std::array<Vector3, 4> p{topology.vertices[fv.vertex[0]], topology.vertices[fv.vertex[1]],
                               topology.vertices[fv.vertex[2]], topology.vertices[fv.vertex[3]]};
      const auto [phiIntegral4, areaIntegral4] = integrate(p, 4);
      const auto [phiIntegral2, areaIntegral2] = integrate(p, 2);
      const Real sign = (face.owner() == cell.id()) ? 1.0 : -1.0;
      const Vector3 exactFace = phiIntegral4 * sign;
      const Vector3 sfCell = (face.owner() == cell.id()) ? face.areaVector()
                                                         : (face.areaVector() * -1.0);
      const Vector3 pointFace = sfCell * phiOf(face.centroid());
      exactSum = exactSum + exactFace;
      pointSum = pointSum + pointFace;
      // Diagnostics: is the rule converged, does integral(n dS) match the stored S_f, and how
      // warped is the face (deviation of p2 from the plane of p0,p1,p3)?
      worstRuleCheck = std::max(worstRuleCheck, magnitude(phiIntegral4 - phiIntegral2) /
                                                    std::max(1e-300, magnitude(phiIntegral4)));
      worstAreaCheck = std::max(worstAreaCheck, magnitude(areaIntegral4 - face.areaVector()) /
                                                   face.area());
      const Vector3 nPlane = cross(p[1] - p[0], p[3] - p[0]);
      const Real nm = magnitude(nPlane);
      if (nm > 0.0) {
        worstWarp = std::max(worstWarp, std::abs(dot(p[2] - p[0], nPlane) / nm) /
                                            std::sqrt(face.area()));
      }
      const Real scale = std::max(1e-300, magnitude(exactFace));
      worstFaceRelative = std::max(worstFaceRelative, magnitude(pointFace - exactFace) / scale);
    }
    // For a linear field the exact surface integral must give V * grad(phi) exactly.
    const Vector3 exactGradient = kA * cell.volume();
    worstCellRelative = std::max(worstCellRelative,
                                 magnitude(pointSum - exactSum) / magnitude(exactGradient));
  }
  std::printf("Q   %-26s n %2zu | max face warp/sqrt(A) %.3e | 4x4 vs 2x2 rule %.2e | "
              "int(n dS) vs S_f %.2e | single-point face error %.3e | per-cell gradient error "
              "%.3e\n",
              label, static_cast<std::size_t>(n), worstWarp, worstRuleCheck, worstAreaCheck,
              worstFaceRelative, worstCellRelative);
}

}  // namespace

int main() {
  std::printf("# INV-001 step 11: is one face centroid enough for int(phi n dS) on a warped face?\n");
  std::printf("# phi = (%.1f, %.1f, %.1f) . x + %.1f (linear -- must be exact)\n", kA.x, kA.y, kA.z,
              kB);
  study(8, 0.0, "3D 8^3 Cartesian");
  study(8, 0.05, "3D 8^3 deformed");
  study(16, 0.05, "3D 16^3 deformed");
  study(32, 0.05, "3D 32^3 deformed");
  return 0;
}
