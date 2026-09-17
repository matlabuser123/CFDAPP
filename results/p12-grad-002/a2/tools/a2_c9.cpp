// P12-GRAD-002 A2 dry-run probe -- C9's clauses on the sinusoidally DEFORMED 3D family that no
// earlier run measured (A1 measured C1 and C2 there only):
//   C3(b) on deformed 3D: grad(x^3) (A1's convergence field, exact per-patch conditions) on
//         8^3 -> 16^3 -> 32^3; observed order in L-infinity and volume-weighted L2 (frozen: >= 1.8,
//         and not below the pre-GRAD-002 order by more than 0.1).
//   C4a on deformed 3D: the deformed mesh and its rigidly translated copy (offset (0.005, 0.0025,
//         0.00125)); geometry gate C_g = max |x_B - o - x_A| / h <= 1e-6; constant, linear and
//         axial-quadratic fields; Delta <= E (A1 envelope).
//   C6-A1 on deformed 3D: the same at L = 1e-3, 1, 1e3 (offset 0.005 L); rho = Delta / E <= 1 at
//         every scale, rho spread (exact zeros excluded) <= 100 per field.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"

using namespace cfd;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// The deformed box of side L (A1/INV-001's sinusoidal deformation, scaled), then translated by o.
class DeformedTranslated final : public mesh::PrescribedMotion {
 public:
  DeformedTranslated(Real length, const Vector3& offset)
      : sin_(Vector3{0, 0, 0}, Vector3{length, length, length},
             Vector3{0.05 * length, 0.025 * length, -0.0375 * length}, constants::twoPi / 0.4),
        offset_(offset) {}
  [[nodiscard]] Vector3 position(const Vector3& x, Real elapsed) const override {
    return x + ((sin_.position(x, 0.1) - x + offset_) * elapsed);
  }
  [[nodiscard]] std::string description() const override { return "deformed + translated"; }

 private:
  mesh::SinusoidalMotion sin_;
  Vector3 offset_;
};

mesh::Mesh deformed(Index n, Real length, const Vector3& offset) {
  mesh::Mesh m = mesh::MeshGeometry::createCartesian3D(n, n, n, length, length, length);
  mesh::MeshMotion motion(m, std::make_shared<DeformedTranslated>(length, offset));
  (void)motion.advance(1.0);
  return m;
}

enum class Kind { Constant, Linear, Quadratic, Cubic };

Real value(Kind k, const Vector3& x, const Vector3& o, Real length) {
  const Vector3 r = (x - o) * (1.0 / length);  // dimensionless position
  switch (k) {
    case Kind::Constant:
      return 2.5;
    case Kind::Linear:
      return 0.7 + (1.3 * r.x) - (0.9 * r.y) + (0.4 * r.z);
    case Kind::Quadratic:
      return 0.5 * r.x * r.x;
    case Kind::Cubic:
    default:
      return r.x * r.x * r.x;
  }
}

Vector3 gradientOf(Kind k, const Vector3& x, const Vector3& o, Real length) {
  const Vector3 r = (x - o) * (1.0 / length);
  switch (k) {
    case Kind::Constant:
      return Vector3{};
    case Kind::Linear:
      return Vector3{1.3, -0.9, 0.4} * (1.0 / length);
    case Kind::Quadratic:
      return Vector3{r.x / length, 0, 0};
    case Kind::Cubic:
    default:
      return Vector3{3.0 * r.x * r.x / length, 0, 0};
  }
}

const char* nameOf(Kind k) {
  switch (k) {
    case Kind::Constant: return "constant";
    case Kind::Linear: return "linear";
    case Kind::Quadratic: return "quadratic";
    default: return "cubic";
  }
}

// Exact per-patch conditions: Neumann a.n for constant/linear; for the axial fields FixedValue on
// constant-x planes, zero gradient elsewhere. `exact` false if a patch is not the plane needed.
boundary::BoundaryConditionSet conditions(const mesh::Mesh& m, Kind k, const Vector3& o,
                                          Real length, bool& exact) {
  boundary::BoundaryConditionSet set;
  exact = true;
  for (const auto& patch : m.boundaryPatches()) {
    const auto& first = m.face(patch.faceIds().front());
    const Vector3 n0 = first.areaVector() * (1.0 / first.area());
    bool flat = true;
    for (const Index id : patch.faceIds()) {
      const Vector3 n = m.face(id).areaVector() * (1.0 / m.face(id).area());
      if (magnitude(n - n0) > 1e-12) flat = false;
    }
    if (k == Kind::Constant || k == Kind::Linear) {
      if (!flat) exact = false;
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(
                                   dot(gradientOf(k, first.centroid(), o, length), n0)));
      continue;
    }
    const bool constantX = flat && std::abs(std::abs(n0.x) - 1.0) < 1e-12;
    if (constantX) {
      const Real x = first.centroid().x;
      for (const Index id : patch.faceIds()) {
        if (std::abs(m.face(id).centroid().x - x) > 1e-12 * std::max(1.0, std::abs(x))) {
          exact = false;
        }
      }
      set.set(m, patch.name(), std::make_unique<boundary::FixedValue>(
                                   value(k, Vector3{x, 0, 0}, Vector3{o.x, 0, 0}, length)));
    } else {
      if (!(flat && std::abs(n0.x) <= 1e-12)) exact = false;
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return set;
}

fields::VectorField gradient(const mesh::Mesh& m, Kind k, const Vector3& o, Real length,
                             bool& exact) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& c : m.cells()) phi[c.id()] = value(k, c.centroid(), o, length);
  const auto bc = conditions(m, k, o, length, exact);
  return discretization::gradient(m, phi, bc, discretization::GradientScheme::GreenGauss);
}

Real envelope(const mesh::Mesh& m, Kind k, const Vector3& o, Real length, Real cg) {
  Real phi = 0.0;
  Real grad = 0.0;
  Real conditioning = 0.0;
  for (const auto& c : m.cells()) {
    phi = std::max(phi, std::abs(value(k, c.centroid(), o, length)));
    grad = std::max(grad, magnitude(gradientOf(k, c.centroid(), o, length)));
    Real area = 0.0;
    for (const Index id : c.faceIds()) area += m.face(id).area();
    conditioning = std::max(conditioning, area / c.volume());
  }
  for (const auto& f : m.faces()) {
    if (f.isBoundary()) phi = std::max(phi, std::abs(value(k, f.centroid(), o, length)));
  }
  return (kEps * 8.0 * phi * conditioning) + (100.0 * cg * grad);
}

// C4a/C6 row: returns rho per field (constant, linear, quadratic).
std::vector<Real> translation(const char* tag, Index n, Real length, const Vector3& offset) {
  const mesh::Mesh a = deformed(n, length, Vector3{});
  const mesh::Mesh b = deformed(n, length, offset);
  const Real h = length / static_cast<Real>(n);
  Real cg = 0.0;
  Real volume = 0.0;
  Real faceCentroid = 0.0;
  Real area = 0.0;
  for (const auto& c : a.cells()) {
    cg = std::max(cg, magnitude(b.cell(c.id()).centroid() - offset - c.centroid()) / h);
    volume = std::max(volume, std::abs(b.cell(c.id()).volume() - c.volume()) / c.volume());
  }
  for (const auto& f : a.faces()) {
    faceCentroid =
        std::max(faceCentroid, magnitude(b.face(f.id()).centroid() - offset - f.centroid()) / h);
    area = std::max(area, magnitude(b.face(f.id()).areaVector() - f.areaVector()) / f.area());
  }
  const bool valid = cg <= 1e-6 && volume <= 1e-6 && faceCentroid <= 1e-6 && area <= 1e-6;
  std::printf("%s  deformed %2zu^3 L=%-6g geometry: volume %.2e centroid %.2e faceCentroid %.2e "
              "area %.2e -> %s\n",
              tag, static_cast<std::size_t>(n), length, volume, cg, faceCentroid, area,
              valid ? "VALID" : "INVALID (excluded)");
  std::vector<Real> rho;
  for (const Kind k : {Kind::Constant, Kind::Linear, Kind::Quadratic}) {
    bool ea = false;
    bool eb = false;
    const auto ga = gradient(a, k, Vector3{}, length, ea);
    const auto gb = gradient(b, k, offset, length, eb);
    Real delta = 0.0;
    for (Index i = 0; i < a.numberOfCells(); ++i) delta = std::max(delta, magnitude(gb[i] - ga[i]));
    const Real e = envelope(a, k, Vector3{}, length, cg);
    const Real r = delta / e;
    rho.push_back(r);
    std::printf("%s  deformed %2zu^3 L=%-6g %-9s delta %.3e E %.3e rho %.3e %s%s\n", tag,
                static_cast<std::size_t>(n), length, nameOf(k), delta, e, r,
                !valid ? "DIAGNOSTIC" : (r <= 1.0 ? "PASS" : "FAIL"),
                (ea && eb) ? "" : " [BC NOT EXACT -- INVALID]");
  }
  return rho;
}

}  // namespace

int main() {
  std::printf("# A2 C9 dry-run: C3(b), C4a, C6-A1 on the sinusoidally deformed 3D family\n");
  // ---- C3(b) ----
  std::vector<Real> linf;
  std::vector<Real> l2;
  for (const Index n : {8u, 16u, 32u}) {
    const mesh::Mesh m = deformed(n, 1.0, Vector3{});
    bool exact = false;
    const auto g = gradient(m, Kind::Cubic, Vector3{}, 1.0, exact);
    Real worst = 0.0;
    Real sum = 0.0;
    Real vol = 0.0;
    Real worstBoundary = 0.0;
    for (const auto& c : m.cells()) {
      const Real e = magnitude(g[c.id()] - gradientOf(Kind::Cubic, c.centroid(), Vector3{}, 1.0));
      worst = std::max(worst, e);
      sum += e * e * c.volume();
      vol += c.volume();
      bool boundary = false;
      for (const Index id : c.faceIds()) boundary = boundary || m.face(id).isBoundary();
      if (boundary) worstBoundary = std::max(worstBoundary, e);
    }
    linf.push_back(worst);
    l2.push_back(std::sqrt(sum / vol));
    std::printf("C3b deformed %2zu^3 grad(x^3): Linf %.4e (boundary cells %.4e) L2 %.4e%s\n",
                static_cast<std::size_t>(n), worst, worstBoundary, l2.back(),
                exact ? "" : " [BC NOT EXACT -- INVALID]");
  }
  for (std::size_t i = 1; i < linf.size(); ++i) {
    const Real pInf = std::log2(linf[i - 1] / linf[i]);
    const Real p2 = std::log2(l2[i - 1] / l2[i]);
    std::printf("C3b deformed order %zu->%zu: Linf %.3f L2 %.3f (frozen >= 1.8) %s\n",
                static_cast<std::size_t>(8u << (i - 1)), static_cast<std::size_t>(8u << i), pInf,
                p2, (pInf >= 1.8 && p2 >= 1.8) ? "PASS" : "FAIL");
  }
  // ---- C4a ----
  const Vector3 small{0.005, 0.0025, 0.00125};
  for (const Index n : {8u, 16u, 32u}) (void)translation("C4a", n, 1.0, small);
  // ---- C6-A1 ----
  for (const Index n : {8u, 16u}) {
    std::vector<std::vector<Real>> perScale;
    for (const Real length : {1e-3, 1.0, 1e3}) {
      perScale.push_back(translation("C6 ", n, length, small * length));
    }
    const char* names[] = {"constant", "linear", "quadratic"};
    for (std::size_t f = 0; f < 3; ++f) {
      Real lo = 1e300;
      Real hi = 0.0;
      bool allWithin = true;
      for (const auto& s : perScale) {
        allWithin = allWithin && s[f] <= 1.0;
        if (s[f] <= 0.0) continue;
        lo = std::min(lo, s[f]);
        hi = std::max(hi, s[f]);
      }
      const Real spread = (hi > 0.0 && lo < 1e300) ? hi / lo : 1.0;
      std::printf("C6s deformed %2zu^3 %-9s rho spread %.3e (bound 100), every rho <= 1: %s -> %s\n",
                  static_cast<std::size_t>(n), names[f], spread, allWithin ? "yes" : "no",
                  (spread <= 100.0 && allWithin) ? "PASS" : "FAIL");
    }
  }
  return 0;
}
