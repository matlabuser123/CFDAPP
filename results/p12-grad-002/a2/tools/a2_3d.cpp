// P12-GRAD-002 A2 dry-run probe -- C2/C9's "3D deformed" clause.
//
// C2 froze "linear field: relative error <= 1e-9 on the distorted meshes (Q16, 3D deformed)" with
// the derivation "the correction vanishes identically for a linear field once the lagged gradient
// is converged, leaving plain Green-Gauss with exact face values". That derivation has two
// premises, and this probe measures each separately:
//
//   P1  the lagged sweeps converge (production stops after kGreenGaussSkewCorrectionSweeps = 4);
//   P2  plain Green-Gauss with exact face values, (1/V) sum_f phi(x_f) S_f, is exact for a linear
//       phi. P2 holds for PLANAR faces (x_f = area centroid). For a WARPED (bilinear) face a single
//       point cannot represent integral(phi n dS), whatever the boundary treatment.
//
// Families (every patch flat, so the per-patch Neumann values are exact for the linear field):
//   planar  createCartesian3D + a vertex map whose faces are all planar but whose cells are graded,
//           skewed (interior skew vector != 0) and non-orthogonal at every boundary (tilted z
//           patches, sheared x columns):  x' = x + s a sin(pi x) + s a sin(2 pi y + 0.3) sin(pi x),
//           y' = y + s a sin(pi y),  z' = z + 0.3 s y'  (a = 0.03, Q16's vertex amplitude).
//   warped  A1's sinusoidally deformed box (SinusoidalMotion, amplitude (A, A/2, -3A/4)).
//   2D references: Q16 (the mesh C2's 1e-9 was established on) and P12-NUM-003's distorted
//           meshes at 0.45 h (the worst of the range kGreenGaussSkewCorrectionSweeps was chosen on).
//
// Independent reference (no production gradient code): each bilinear face's integral(phi n dS)
// and integral(x . n dS) by a 4x4 Gauss rule (exact: degree <= 2 per variable), giving per cell
//   D_P   = (1/V_P) (sum_f s phi(x_f) S_f - sum_f s integral(phi n dS))   the warp defect
//   Vex_P = (1/3) sum_f s integral(x . n dS)                              the exact volume
// so plain Green-Gauss with exact face values has the error  PG_P = D_P + (Vex_P / V_P - 1) a.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "DistortedMesh.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"

using namespace cfd;

namespace {

const Vector3 kA{1.3, -0.9, 0.4};
constexpr Real kB = 0.7;
Real phiOf(const Vector3& x) { return dot(kA, x) + kB; }

class PlanarSkewMotion final : public mesh::PrescribedMotion {
 public:
  explicit PlanarSkewMotion(Real s) : s_(s) {}
  [[nodiscard]] Vector3 position(const Vector3& x, Real elapsed) const override {
    return x + ((target(x) - x) * elapsed);  // elapsed in [0, 1]; exactly x at 0
  }
  [[nodiscard]] std::string description() const override { return "planar skew"; }

 private:
  [[nodiscard]] Vector3 target(const Vector3& p) const {
    const Real pi = constants::pi;
    const Real a = 0.03 * s_;
    const Real y = p.y + (a * std::sin(pi * p.y));
    const Real x = p.x + (a * std::sin(pi * p.x)) +
                   (a * std::sin((2.0 * pi * p.y) + 0.3) * std::sin(pi * p.x));
    return Vector3{x, y, p.z + (0.3 * s_ * y)};
  }
  Real s_;
};

mesh::Mesh planar(Index n, Real s) {
  mesh::Mesh m = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  if (s != 0.0) {
    mesh::MeshMotion motion(m, std::make_shared<PlanarSkewMotion>(s));
    (void)motion.advance(1.0);
  }
  return m;
}

mesh::Mesh warped(Index n, Real amplitude) {
  mesh::Mesh m = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  if (amplitude != 0.0) {
    mesh::MeshMotion motion(
        m, std::make_shared<mesh::SinusoidalMotion>(
               Vector3{0, 0, 0}, Vector3{1, 1, 1},
               Vector3{amplitude, 0.5 * amplitude, -0.75 * amplitude}, constants::twoPi / 0.4));
    (void)motion.advance(0.1);
  }
  return m;
}

std::vector<Vector3> q16Vertices() {
  std::vector<Vector3> v;
  const Index n = 16;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real x = static_cast<Real>(i) / static_cast<Real>(n);
      const Real y = static_cast<Real>(j) / static_cast<Real>(n);
      const Real pi = constants::pi;
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return v;
}

boundary::BoundaryConditionSet exactNeumann(const mesh::Mesh& m, bool& exact) {
  boundary::BoundaryConditionSet set;
  exact = true;
  for (const auto& patch : m.boundaryPatches()) {
    const auto& first = m.face(patch.faceIds().front());
    const Vector3 n0 = first.areaVector() * (1.0 / first.area());
    for (const Index id : patch.faceIds()) {
      const Vector3 n = m.face(id).areaVector() * (1.0 / m.face(id).area());
      if (magnitude(n - n0) > 1e-12) exact = false;
    }
    set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(dot(kA, n0)));
  }
  return set;
}

// ---- independent bilinear-face quadrature (INV-001 inv_quadrature.cpp's rule) ----
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
struct FaceIntegrals {
  Vector3 phiN{};  // integral(phi n dS)
  Real xDotN{};    // integral(x . n dS)
  Vector3 nDs{};   // integral(n dS)
};
FaceIntegrals integrate(const std::array<Vector3, 4>& p) {
  const Real x1 = std::sqrt((3.0 / 7.0) - ((2.0 / 7.0) * std::sqrt(6.0 / 5.0)));
  const Real x2 = std::sqrt((3.0 / 7.0) + ((2.0 / 7.0) * std::sqrt(6.0 / 5.0)));
  const Real w1 = (18.0 + std::sqrt(30.0)) / 36.0;
  const Real w2 = (18.0 - std::sqrt(30.0)) / 36.0;
  const std::array<Real, 4> node{0.5 - (0.5 * x2), 0.5 - (0.5 * x1), 0.5 + (0.5 * x1),
                                 0.5 + (0.5 * x2)};
  const std::array<Real, 4> weight{0.5 * w2, 0.5 * w1, 0.5 * w1, 0.5 * w2};
  FaceIntegrals r;
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      const Vector3 x = bilinearPoint(p, node[i], node[j]);
      const Vector3 n = cross(bilinearDu(p, node[j]), bilinearDv(p, node[i]));
      const Real w = weight[i] * weight[j];
      r.phiN = r.phiN + (n * (w * phiOf(x)));
      r.xDotN += w * dot(x, n);
      r.nDs = r.nDs + (n * w);
    }
  }
  return r;
}

struct Geometry {
  Real warp{0};      // max face warp / sqrt(A)
  Real skew{0};      // max interior |skew vector| / |d|
  Real mf{0};        // max boundary misalignment |d x S| / (|d||S|)
  Real tMin{1}, tMax{0};
  Real alpha{0};     // max_P (1/V_P) sum_{interior f} |S_f| |skew_f|
  Real volumeTerm{0};  // max |Vex/V - 1|
  Real areaCheck{0};   // max |integral(n dS) - S_f| / A
  bool planarMeasured{false};
};

Geometry geometryOf(const mesh::Mesh& m, std::vector<Vector3>* plainGGError) {
  Geometry g;
  for (const auto& f : m.faces()) {
    if (f.isBoundary()) {
      const Vector3 d = f.centroid() - m.cell(f.owner()).centroid();
      g.mf = std::max(g.mf, magnitude(cross(d, f.areaVector())) /
                                (magnitude(d) * f.area()));
      continue;
    }
    const auto c = mesh::MeshGeometry::ownerNeighborCrossing(m, f);
    if (!c) continue;
    const Real d = mesh::MeshGeometry::ownerNeighborDistance(m, f);
    g.skew = std::max(g.skew, magnitude(c->skewVector) / d);
    g.tMin = std::min(g.tMin, c->t);
    g.tMax = std::max(g.tMax, c->t);
  }
  for (const auto& cell : m.cells()) {
    Real a = 0.0;
    for (const Index id : cell.faceIds()) {
      const auto& f = m.face(id);
      if (f.isBoundary()) continue;
      const auto c = mesh::MeshGeometry::ownerNeighborCrossing(m, f);
      if (c) a += f.area() * magnitude(c->skewVector);
    }
    g.alpha = std::max(g.alpha, a / cell.volume());
  }
  if (m.dimension() != 3) return g;
  g.planarMeasured = true;
  const auto topology = mesh::MeshGeometry::structuredTopology(m);
  std::vector<FaceIntegrals> integrals(m.numberOfFaces());
  for (const auto& f : m.faces()) {
    const auto& fv = topology.faces[f.id()];
    const std::array<Vector3, 4> p{topology.vertices[fv.vertex[0]],
                                   topology.vertices[fv.vertex[1]],
                                   topology.vertices[fv.vertex[2]],
                                   topology.vertices[fv.vertex[3]]};
    integrals[f.id()] = integrate(p);
    g.areaCheck = std::max(g.areaCheck, magnitude(integrals[f.id()].nDs - f.areaVector()) / f.area());
    const Vector3 nPlane = cross(p[1] - p[0], p[3] - p[0]);
    g.warp = std::max(g.warp, std::abs(dot(p[2] - p[0], nPlane)) / magnitude(nPlane) /
                                  std::sqrt(f.area()));
  }
  if (plainGGError != nullptr) plainGGError->assign(m.numberOfCells(), Vector3{});
  for (const auto& cell : m.cells()) {
    Vector3 pointSum{};
    Real volume = 0.0;
    for (const Index id : cell.faceIds()) {
      const auto& f = m.face(id);
      const Real s = (f.owner() == cell.id()) ? 1.0 : -1.0;
      pointSum = pointSum + (f.areaVector() * (s * phiOf(f.centroid())));
      volume += s * integrals[id].xDotN / 3.0;
    }
    g.volumeTerm = std::max(g.volumeTerm, std::abs((volume / cell.volume()) - 1.0));
    if (plainGGError != nullptr) {
      (*plainGGError)[cell.id()] = (pointSum * (1.0 / cell.volume())) - kA;
    }
  }
  return g;
}

// The same warp defect D_P, without the volume term, for the report.
std::vector<Vector3> warpDefect(const mesh::Mesh& m) {
  const auto topology = mesh::MeshGeometry::structuredTopology(m);
  std::vector<Vector3> d(m.numberOfCells(), Vector3{});
  for (const auto& cell : m.cells()) {
    Vector3 sum{};
    for (const Index id : cell.faceIds()) {
      const auto& f = m.face(id);
      const auto& fv = topology.faces[id];
      const std::array<Vector3, 4> p{topology.vertices[fv.vertex[0]],
                                     topology.vertices[fv.vertex[1]],
                                     topology.vertices[fv.vertex[2]],
                                     topology.vertices[fv.vertex[3]]};
      const Real s = (f.owner() == cell.id()) ? 1.0 : -1.0;
      sum = sum + (((f.areaVector() * phiOf(f.centroid())) - integrate(p).phiN) * s);
    }
    d[cell.id()] = sum * (1.0 / cell.volume());
  }
  return d;
}

Real maxNorm(const std::vector<Vector3>& v) {
  Real w = 0.0;
  for (const auto& x : v) w = std::max(w, magnitude(x));
  return w;
}

Vector3 referenceGradient(const mesh::Mesh& m) {
  return m.dimension() == 3 ? kA : Vector3{kA.x, kA.y, 0.0};
}

// The exact error recursion of the lagged sweeps for a LINEAR field (K >= 1):
//   e_{K+1} = PG + A e_K,
// where PG is plain Green-Gauss with exact face values (the warp defect; 0 on planar faces) and A
// collects every term that uses the previous sweep's gradient:
//   interior skewed face   phi_f error = ebar_f . skew_f,  ebar_f = (1-t) e_O + t e_N
//   oblique Neumann face   phi_b error = e_P . d_t
//   GRAD-002 opposite-face correction  -1/2 w(1-w) L^2 * 2 (dphi_B'/t)/(t+L),
//                          dphi_B' = e_P . (d_t [Neumann faces only] + (B' - x_b))
// With t in [0,1], |ebar_f| <= max(|e_O|, |e_N|), so by the triangle inequality, per cell,
//   |e_{K+1,P} - PG_P| <= bound_P(e_K)
//     = (1/V_P) [ sum_int |S_f||skew_f| max(|e_O|,|e_N|)
//               + sum_bnd (|S_b||d_t| + |S_opp| w(1-w) L^2 |d_t + B' - x_b| / (t (t+L))) |e_P| ].
// B' and t are computed here from their documented definition (MeshGeometry.hpp), not by the
// production helper, so the bound also holds for libraries that do not have it (the pre-GRAD-002
// operator has no correction term; the bound then over-counts, which keeps it valid).
std::vector<Real> feedbackBound(const mesh::Mesh& m, const boundary::BoundaryConditionSet& bcs,
                                const std::vector<Vector3>& e) {
  std::vector<Real> bound(m.numberOfCells(), 0.0);
  for (const auto& cell : m.cells()) {
    const Index p = cell.id();
    Real sum = 0.0;
    for (const Index id : cell.faceIds()) {
      const auto& f = m.face(id);
      if (!f.isBoundary()) {
        const auto c = mesh::MeshGeometry::ownerNeighborCrossing(m, f);
        if (c && c->skewVector != Vector3{}) {
          const Index q = (f.owner() == p) ? *f.neighbor() : f.owner();
          sum += f.area() * magnitude(c->skewVector) * std::max(magnitude(e[p]), magnitude(e[q]));
        }
        continue;
      }
      const bool valueFace = discretization::prescribesBoundaryValue(
          boundary::boundaryConditionForFace(m, id, bcs).type());
      const Vector3 n = f.areaVector() * (1.0 / f.area());
      Vector3 dt{};
      if (!valueFace) {
        const Vector3 d = f.centroid() - cell.centroid();
        dt = d - (n * dot(d, n));
        sum += f.area() * magnitude(dt) * magnitude(e[p]);
      }
      const auto opp = mesh::MeshGeometry::oppositeInteriorFace(m, cell, f);
      if (!opp) continue;
      const auto& of = m.face(*opp);
      const Index far = (of.owner() == p) ? *of.neighbor() : of.owner();
      const Vector3 dv = m.cell(far).centroid() - cell.centroid();
      const Real length = magnitude(dv);
      const Vector3 dir = dv * (1.0 / length);
      const auto cr = mesh::MeshGeometry::ownerNeighborCrossing(m, of);
      const Real dn = dot(dir, n);
      if (!cr || !(std::abs(dn) > 0.0)) continue;
      const Real t = dot(cell.centroid() - f.centroid(), n) / dn;
      if (!(t > 0.0)) continue;
      const Vector3 bPrime = cell.centroid() - (dir * t);
      const Real w = cr->t;
      sum += of.area() * (w * (1.0 - w) * length * length / (t * (t + length))) *
             magnitude(dt + (bPrime - f.centroid())) * magnitude(e[p]);
    }
    bound[p] = sum / cell.volume();
  }
  return bound;
}

std::vector<Vector3> errorAfter(const mesh::Mesh& m, const fields::ScalarField& phi,
                                const boundary::BoundaryConditionSet& bc, Index k) {
  const auto grad = discretization::greenGaussGradient(m, phi, bc, k);
  std::vector<Vector3> e(m.numberOfCells());
  const Vector3 a = referenceGradient(m);
  for (Index i = 0; i < m.numberOfCells(); ++i) e[i] = grad[i] - a;
  return e;
}

Real maxDiff(const std::vector<Vector3>& a, const std::vector<Vector3>& b) {
  Real w = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) w = std::max(w, magnitude(a[i] - b[i]));
  return w;
}

void study(const char* family, const char* label, const mesh::Mesh& m) {
  constexpr Real kEps = std::numeric_limits<Real>::epsilon();
  const Real ref = magnitude(referenceGradient(m));
  std::vector<Vector3> pg(m.numberOfCells(), Vector3{});  // 2D: faces are planar segments, PG = 0
  const Geometry g = geometryOf(m, m.dimension() == 3 ? &pg : nullptr);
  bool exact = false;
  const auto bc = exactNeumann(m, exact);
  fields::ScalarField phi(m.numberOfCells());
  Real phiMax = 0.0;
  Real conditioning = 0.0;
  for (const auto& c : m.cells()) {
    phi[c.id()] = phiOf(c.centroid());
    phiMax = std::max(phiMax, std::abs(phi[c.id()]));
    Real area = 0.0;
    for (const Index id : c.faceIds()) area += m.face(id).area();
    conditioning = std::max(conditioning, area / c.volume());
  }
  for (const auto& f : m.faces()) {
    if (f.isBoundary()) phiMax = std::max(phiMax, std::abs(phiOf(f.centroid())));
  }
  const Real floor = kEps * 8.0 * phiMax * conditioning;  // A1's frozen K_face term
  std::printf("%s %-24s geom | warp %.3e skew %.3e m_f %.3e t [%.3f,%.3f] alpha %.3e", family,
              label, g.warp, g.skew, g.mf, g.tMin, g.tMax, g.alpha);
  if (g.planarMeasured) {
    std::printf(" | vol %.2e area %.2e | PG %.3e D %.3e", g.volumeTerm, g.areaCheck,
                maxNorm(pg) / ref, maxNorm(warpDefect(m)) / ref);
  }
  std::printf(" | floor %.2e%s\n", floor / ref, exact ? "" : " [BC NOT EXACT -- INVALID]");
  std::printf("%s %-24s K   ", family, label);
  std::vector<std::vector<Vector3>> e;
  for (Index k = 0; k <= 8; ++k) e.push_back(errorAfter(m, phi, bc, k));
  const auto e64 = errorAfter(m, phi, bc, 64);
  for (Index k = 0; k <= 8; ++k) std::printf(" %zu:%.2e", static_cast<std::size_t>(k), maxNorm(e[k]) / ref);
  std::printf(" 64:%.2e\n", maxNorm(e64) / ref);
  // Per-cell recursion check at the production sweep count and at K = 1..8.
  Index failures = 0;
  Index failuresWithoutPG = 0;  // non-vacuity: the same check claiming PG = 0 (exact linearity)
  Real worstRatio = 0.0;
  for (Index k = 1; k <= 8; ++k) {
    const auto bound = feedbackBound(m, bc, e[k - 1]);
    for (Index i = 0; i < m.numberOfCells(); ++i) {
      const Real lhs = magnitude(e[k][i] - pg[i]);
      const Real rhs = bound[i] + floor;
      worstRatio = std::max(worstRatio, lhs / rhs);
      if (lhs > rhs) ++failures;
      if (magnitude(e[k][i]) > rhs) ++failuresWithoutPG;
    }
  }
  if (g.planarMeasured) {  // C2-A2(d)(i): is the warp defect resolved above round-off?
    std::printf("%s %-24s PG/floor %.3e -> %s\n", family, label, maxNorm(pg) / floor,
                maxNorm(pg) > floor ? "RESOLVED" : "not resolved (planar or round-off)");
  }
  const Index kp = discretization::kGreenGaussSkewCorrectionSweeps;
  const auto boundP = feedbackBound(m, bc, e[kp - 1]);
  Real maxBound = 0.0;
  for (const Real b : boundP) maxBound = std::max(maxBound, b);
  std::printf("%s %-24s K=%zu | error %.3e | converged (K=64) %.3e | truncation |e4-e64| %.3e |"
              " |e4-PG| %.3e | bound max %.3e | recursion K=1..8: worst |e-PG|/(bound+floor) %.3e,"
              " cells over %zu -> %s | control PG:=0: cells over %zu\n",
              family, label, static_cast<std::size_t>(kp), maxNorm(e[kp]) / ref,
              maxNorm(e64) / ref, maxDiff(e[kp], e64) / ref, maxDiff(e[kp], pg) / ref,
              maxBound / ref, worstRatio, static_cast<std::size_t>(failures),
              failures == 0 ? "HOLDS" : "VIOLATED", static_cast<std::size_t>(failuresWithoutPG));
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 A2 dry-run: linear-field Green-Gauss on skewed 2D/3D meshes, planar"
              " and warped faces\n# phi = (1.3, -0.9, 0.4) . x + 0.7; all errors relative to"
              " |grad phi| = %.6f; production sweeps K = %zu\n",
              magnitude(kA), static_cast<std::size_t>(discretization::kGreenGaussSkewCorrectionSweeps));
  study("REF2D", "Q16 (C2's mesh)",
        mesh::MeshGeometry::createStructuredQuad2D(16, 16, q16Vertices()));
  study("REF2D", "NUM-003 10x10 0.45h", test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.045));
  study("REF2D", "NUM-003 20x20 0.45h", test::createDistortedQuad2D(20, 20, 1.0, 1.0, 0.0225));
  for (const Index n : {8u, 16u, 32u}) {
    char label[64];
    std::snprintf(label, sizeof(label), "planar s=1 %zu^3", static_cast<std::size_t>(n));
    study("PLANAR", label, planar(n, 1.0));
  }
  for (const Real s : {1e-6, 1e-3, 0.25, 0.5, 2.0}) {
    char label[64];
    std::snprintf(label, sizeof(label), "planar s=%g 16^3", s);
    study("PLANAR", label, planar(16, s));
  }
  for (const Index n : {8u, 16u, 32u}) {
    char label[64];
    std::snprintf(label, sizeof(label), "warped A=0.05 %zu^3", static_cast<std::size_t>(n));
    study("WARPED", label, warped(n, 0.05));
  }
  for (const Real a : {0.0, 1e-8, 1e-6, 1e-4, 1e-3, 1e-2, 0.025}) {
    char label[64];
    std::snprintf(label, sizeof(label), "warped A=%g 16^3", a);
    study("WARPED", label, warped(16, a));
  }
  return 0;
}
