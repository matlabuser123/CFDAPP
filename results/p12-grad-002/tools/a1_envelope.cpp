// P12-GRAD-002 Amendment A1: C1-A1, C4a, C4b and C6-A1 against the frozen A1 envelope
//     E = eps * K_face * Phi * G  +  K_geom * C_g * |grad phi|_ref,   K_face = 8, K_geom = 100.
// Runs identically against base (pre-MESH-007) and new, using only APIs present in both, so the
// same binary logic serves the pre-freeze baseline dry-run and the fresh acceptance run.
//
//   V    geometry validity: independent checks of cell volume, cell centroid, face centroid,
//        face-area vector and face closure -- the domain gate for C4a.
//   C1   analytic error of constant and linear fields vs E, plus the dimensionless artefact bound.
//   C4a  translation difference vs E, inside the valid-geometry domain only.
//   C4b  extreme-coordinate diagnostic: where createStructuredQuad2D's geometry loses validity.
//   C6   scale invariance via rho = Delta/E at L = 1e-3, 1, 1e3.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kFace = 8.0;
constexpr Real kGeom = 100.0;
constexpr Real kGeometryTolerance = 1e-6;
constexpr Real kClosureTolerance = 1e-14;

enum class Kind { Constant, Linear, Quadratic };

struct Field {
  std::string name;
  Kind kind;
  Vector3 origin;
  std::function<Real(const Vector3&)> value;
  std::function<Vector3(const Vector3&)> gradient;
};

Field makeField(Kind kind, bool threeD, const Vector3& o) {
  switch (kind) {
    case Kind::Constant:
      return {"constant", kind, o, [](const Vector3&) { return 2.5; },
              [](const Vector3&) { return Vector3{}; }};
    case Kind::Linear:
      if (threeD) {
        return {"linear", kind, o,
                [o](const Vector3& x) {
                  return 0.7 + (1.3 * (x.x - o.x)) - (0.9 * (x.y - o.y)) + (0.4 * (x.z - o.z));
                },
                [](const Vector3&) { return Vector3{1.3, -0.9, 0.4}; }};
      }
      return {"linear", kind, o,
              [o](const Vector3& x) { return 0.7 + (1.3 * (x.x - o.x)) - (0.9 * (x.y - o.y)); },
              [](const Vector3&) { return Vector3{1.3, -0.9, 0.0}; }};
    case Kind::Quadratic:
    default:
      return {"quadratic", kind, o,
              [o](const Vector3& x) { return 0.5 * (x.x - o.x) * (x.x - o.x); },
              [o](const Vector3& x) { return Vector3{x.x - o.x, 0.0, 0.0}; }};
  }
}

// Exact per-patch conditions: FixedGradient(0) for a constant field; FixedGradient(g.n) for a
// linear field on a flat patch; for the axial quadratic, FixedValue on planes of constant x and
// FixedGradient(0) on patches whose normal is perpendicular to x. `exact` is false if any face of
// any patch is not exactly represented -- the measurement is then invalid, never silently wrong.
boundary::BoundaryConditionSet conditionsFor(const mesh::Mesh& m, const Field& f, bool& exact) {
  boundary::BoundaryConditionSet set;
  exact = true;
  for (const auto& patch : m.boundaryPatches()) {
    const auto& firstFace = m.face(patch.faceIds().front());
    const Vector3 n0 = firstFace.areaVector() * (1.0 / firstFace.area());
    bool flat = true;
    for (const Index id : patch.faceIds()) {
      const Vector3 n = m.face(id).areaVector() * (1.0 / m.face(id).area());
      if (magnitude(n - n0) > 1e-12) flat = false;
    }
    if (f.kind == Kind::Constant) {
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
      continue;
    }
    if (f.kind == Kind::Linear) {
      if (!flat) exact = false;
      set.set(m, patch.name(),
              std::make_unique<boundary::FixedGradient>(dot(f.gradient(firstFace.centroid()), n0)));
      continue;
    }
    const bool constantX = flat && std::abs(std::abs(n0.x) - 1.0) < 1e-12;
    const Real x = firstFace.centroid().x;
    if (constantX) {
      for (const Index id : patch.faceIds()) {
        if (std::abs(m.face(id).centroid().x - x) > 1e-12 * std::max(1.0, std::abs(x))) {
          exact = false;
        }
      }
      set.set(m, patch.name(), std::make_unique<boundary::FixedValue>(f.value(Vector3{x, 0, 0})));
    } else {
      if (!(flat && std::abs(n0.x) <= 1e-12)) exact = false;
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return set;
}

fields::ScalarField sample(const mesh::Mesh& m, const Field& f) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& c : m.cells()) phi[c.id()] = f.value(c.centroid());
  return phi;
}

fields::VectorField gradientOf(const mesh::Mesh& m, const Field& f, bool& exact) {
  const auto bc = conditionsFor(m, f, exact);
  return discretization::gradient(m, sample(m, f), bc, discretization::GradientScheme::GreenGauss);
}

// Phi: the cancellation scale of the Green-Gauss sum.
Real fieldMagnitude(const mesh::Mesh& m, const Field& f) {
  Real phi = 0.0;
  for (const auto& c : m.cells()) phi = std::max(phi, std::abs(f.value(c.centroid())));
  for (const auto& face : m.faces()) {
    if (face.isBoundary()) phi = std::max(phi, std::abs(f.value(face.centroid())));
  }
  return phi;
}

Real gradientReference(const mesh::Mesh& m, const Field& f) {
  Real g = 0.0;
  for (const auto& c : m.cells()) g = std::max(g, magnitude(f.gradient(c.centroid())));
  return g;
}

// G = max_cell sum_f |S_f| / V  (1/length).
Real geometricConditioning(const mesh::Mesh& m) {
  Real worst = 0.0;
  for (const auto& c : m.cells()) {
    Real area = 0.0;
    for (const Index id : c.faceIds()) area += m.face(id).area();
    worst = std::max(worst, area / c.volume());
  }
  return worst;
}

Real envelope(const mesh::Mesh& m, const Field& f, Real cg) {
  return (kEps * kFace * fieldMagnitude(m, f) * geometricConditioning(m)) +
         (kGeom * cg * gradientReference(m, f));
}

struct Geometry {
  Real volume{0.0};
  Real centroid{0.0};
  Real faceCentroid{0.0};
  Real areaVector{0.0};
  Real closure{0.0};
  bool valid{false};
};

Real closureOf(const mesh::Mesh& m) {
  Real worst = 0.0;
  for (const auto& c : m.cells()) {
    Vector3 sum{};
    Real total = 0.0;
    for (const Index id : c.faceIds()) {
      const auto& face = m.face(id);
      sum = sum + ((face.owner() == c.id()) ? face.areaVector() : (face.areaVector() * -1.0));
      total += face.area();
    }
    if (total > 0.0) worst = std::max(worst, magnitude(sum) / total);
  }
  return worst;
}

void finish(Geometry& g) {
  g.closure = std::max(g.closure, 0.0);
  g.valid = g.volume <= kGeometryTolerance && g.centroid <= kGeometryTolerance &&
            g.faceCentroid <= kGeometryTolerance && g.areaVector <= kGeometryTolerance &&
            g.closure <= kClosureTolerance;
}

// Translation consistency: how far the translated mesh's geometry is from the original's,
// shifted -- measurable on any mesh without a closed form. h normalizes the centroid terms.
Geometry translationGeometry(const mesh::Mesh& a, const mesh::Mesh& b, const Vector3& offset,
                             Real h) {
  Geometry g;
  for (const auto& c : a.cells()) {
    const auto& d = b.cell(c.id());
    g.volume = std::max(g.volume, std::abs(d.volume() - c.volume()) / c.volume());
    g.centroid = std::max(g.centroid, magnitude(d.centroid() - offset - c.centroid()) / h);
  }
  for (const auto& face : a.faces()) {
    const auto& other = b.face(face.id());
    g.faceCentroid =
        std::max(g.faceCentroid, magnitude(other.centroid() - offset - face.centroid()) / h);
    g.areaVector =
        std::max(g.areaVector, magnitude(other.areaVector() - face.areaVector()) / face.area());
  }
  g.closure = std::max(closureOf(a), closureOf(b));
  finish(g);
  return g;
}

// Absolute geometry accuracy of a Cartesian-family mesh against its closed form.
Geometry exactGeometry(const mesh::Mesh& m, Index n, Real length, const Vector3& offset) {
  Geometry g;
  const Real h = length / static_cast<Real>(n);
  const Real exactVolume = h * h;
  for (Index j = 0; j < n; ++j) {
    for (Index i = 0; i < n; ++i) {
      const auto& c = m.cell((j * n) + i);
      const Vector3 exact{((static_cast<Real>(i) + 0.5) * h) + offset.x,
                          ((static_cast<Real>(j) + 0.5) * h) + offset.y, 0.0};
      g.volume = std::max(g.volume, std::abs(c.volume() - exactVolume) / exactVolume);
      g.centroid = std::max(g.centroid, magnitude(c.centroid() - exact) / h);
    }
  }
  g.closure = closureOf(m);
  finish(g);
  return g;
}

std::vector<Vector3> cartesian(Index n, Real length, const Vector3& o) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n) * length) + o.x,
                          (static_cast<Real>(j) / static_cast<Real>(n) * length) + o.y, 0.0});
    }
  }
  return v;
}

std::vector<Vector3> distorted(Index n, const Vector3& o) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real x = static_cast<Real>(i) / static_cast<Real>(n);
      const Real y = static_cast<Real>(j) / static_cast<Real>(n);
      const Real pi = constants::pi;
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)) + o.x,
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)) + o.y, 0.0});
    }
  }
  return v;
}

mesh::Mesh quad(Index n, Real length, const Vector3& o) {
  return mesh::MeshGeometry::createStructuredQuad2D(n, n, cartesian(n, length, o));
}

Real maxDifference(const fields::VectorField& a, const fields::VectorField& b) {
  Real worst = 0.0;
  for (Index i = 0; i < a.size(); ++i) worst = std::max(worst, magnitude(a[i] - b[i]));
  return worst;
}

Real maxError(const mesh::Mesh& m, const fields::VectorField& g, const Field& f) {
  Real worst = 0.0;
  for (const auto& c : m.cells()) {
    worst = std::max(worst, magnitude(g[c.id()] - f.gradient(c.centroid())));
  }
  return worst;
}

std::vector<Field> allFields(bool threeD, const Vector3& o) {
  return {makeField(Kind::Constant, threeD, o), makeField(Kind::Linear, threeD, o),
          makeField(Kind::Quadratic, threeD, o)};
}

// C1: analytic error against E, with C_g from the mesh's own absolute geometry accuracy.
void reportC1(const char* label, const mesh::Mesh& m, Real cg, Real h, bool threeD) {
  for (const auto& f : allFields(threeD, Vector3{})) {
    if (f.kind == Kind::Quadratic) continue;  // C1 covers constant and linear fields
    bool exact = false;
    const auto g = gradientOf(m, f, exact);
    const Real delta = maxError(m, g, f);
    const Real e = envelope(m, f, cg);
    const Real artefact = delta * h / fieldMagnitude(m, f);
    const bool pass = exact && delta <= e && artefact <= 1e-9;
    std::printf("C1  %-38s %-9s delta %.3e E %.3e ratio %.3e | artefact %.3e %s%s\n", label,
                f.name.c_str(), delta, e, (e > 0.0) ? delta / e : 0.0, artefact,
                pass ? "PASS" : "FAIL", exact ? "" : " [BC NOT EXACT -- INVALID]");
  }
}

// C4a / C4b: translation difference against E, gated by geometry validity.
std::vector<Real> reportTranslation(const char* tag, const char* label, const mesh::Mesh& a,
                                    const mesh::Mesh& b, const Vector3& offset, Real h,
                                    bool threeD) {
  const Geometry geo = translationGeometry(a, b, offset, h);
  std::printf("V   %-38s volume %.3e centroid %.3e faceCentroid %.3e area %.3e closure %.3e -> %s\n",
              label, geo.volume, geo.centroid, geo.faceCentroid, geo.areaVector, geo.closure,
              geo.valid ? "VALID GEOMETRY" : "INVALID GEOMETRY (excluded from C4a)");
  std::vector<Real> ratios;
  for (const auto& f : allFields(threeD, Vector3{})) {
    const Field shifted = makeField(f.kind, threeD, offset);
    bool exactA = false;
    bool exactB = false;
    const auto ga = gradientOf(a, f, exactA);
    const auto gb = gradientOf(b, shifted, exactB);
    const Real delta = maxDifference(ga, gb);
    const Real e = envelope(a, f, geo.centroid);
    const bool valid = exactA && exactB;
    const bool pass = valid && delta <= e;
    const Real ratio = (e > 0.0) ? delta / e : 0.0;
    ratios.push_back(ratio);
    std::printf("%-3s %-38s %-9s delta %.3e E %.3e ratio %.3e %s%s\n", tag, label, f.name.c_str(),
                delta, e, ratio, geo.valid ? (pass ? "PASS" : "FAIL") : "DIAGNOSTIC",
                valid ? "" : " [BC NOT EXACT -- INVALID]");
  }
  return ratios;
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 A1 envelope: K_face %.0f K_geom %.0f eps %.17g\n", kFace, kGeom, kEps);
  const Vector3 small{0.005, 0.0025, 0.0};
  const Vector3 dyadic{1.0 / 128.0, 1.0 / 256.0, 0.0};
  const Vector3 large{1234.5678, 987.6543, 0.0};

  // ---- C1: analytic consistency on every valid-geometry gate mesh ----
  for (const Index n : {16u, 64u, 256u}) {
    char label[96];
    std::snprintf(label, sizeof(label), "2D %zu^2 plain", static_cast<std::size_t>(n));
    const Real h = 1.0 / static_cast<Real>(n);
    reportC1(label, quad(n, 1.0, Vector3{}), exactGeometry(quad(n, 1.0, Vector3{}), n, 1.0, Vector3{}).centroid, h, false);
    std::snprintf(label, sizeof(label), "2D %zu^2 translated small", static_cast<std::size_t>(n));
    reportC1(label, quad(n, 1.0, small), exactGeometry(quad(n, 1.0, small), n, 1.0, small).centroid, h, false);
  }
  reportC1("2D 16^2 translated dyadic", quad(16, 1.0, dyadic),
           exactGeometry(quad(16, 1.0, dyadic), 16, 1.0, dyadic).centroid, 1.0 / 16.0, false);
  reportC1("2D 16^2 L=1e-3 translated", quad(16, 1e-3, Vector3{5e-6, 2.5e-6, 0.0}),
           exactGeometry(quad(16, 1e-3, Vector3{5e-6, 2.5e-6, 0.0}), 16, 1e-3,
                         Vector3{5e-6, 2.5e-6, 0.0})
               .centroid,
           1e-3 / 16.0, false);
  reportC1("2D 16^2 L=1e3 translated", quad(16, 1e3, Vector3{5.0, 2.5, 0.0}),
           exactGeometry(quad(16, 1e3, Vector3{5.0, 2.5, 0.0}), 16, 1e3, Vector3{5.0, 2.5, 0.0})
               .centroid,
           1e3 / 16.0, false);
  {
    const auto graded = mesh::MeshGeometry::createGraded2D(
        16, 16, 1.0, 1.0, mesh::AxisGrading{mesh::GradingType::Geometric, 1.2},
        mesh::AxisGrading{mesh::GradingType::Geometric, 1.2});
    reportC1("2D 16^2 graded 1.2", graded, 0.0, 1.0 / 16.0, false);
  }
  // Q16 is excluded from C1-A1's floating-point envelope (see acceptance_gate_A1.md C1-A1's
  // domain note): on a skewed mesh the linear-field error is NUM-003's four-sweep truncation
  // residual, not round-off. It stays governed by the original C2, reported here against that
  // bound (<= 1e-9 relative, all cells).
  {
    const auto q16 = mesh::MeshGeometry::createStructuredQuad2D(16, 16, distorted(16, Vector3{}));
    for (const auto& f : allFields(false, Vector3{})) {
      if (f.kind == Kind::Constant) continue;
      bool exact = false;
      const auto g = gradientOf(q16, f, exact);
      const Real relative = maxError(q16, g, f) / gradientReference(q16, f);
      const bool linear = f.kind == Kind::Linear;
      std::printf("C2  %-38s %-9s relative %.3e bound %s %s%s\n", "2D Q16 distorted",
                  f.name.c_str(), relative, linear ? "1.000e-09" : "(C3b, not bounded here)",
                  linear ? (relative <= 1e-9 ? "PASS" : "FAIL") : "REPORTED",
                  exact ? "" : " [BC NOT EXACT -- INVALID]");
    }
  }
  reportC1("3D 8^3 exact Cartesian", mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1, 1, 1), 0.0,
           1.0 / 8.0, true);

  // ---- C4a: translation inside the valid-geometry domain ----
  for (const Index n : {16u, 32u, 64u, 128u, 256u}) {
    char label[96];
    const Real h = 1.0 / static_cast<Real>(n);
    std::snprintf(label, sizeof(label), "2D %zu^2 translated small", static_cast<std::size_t>(n));
    reportTranslation("C4a", label, quad(n, 1.0, Vector3{}), quad(n, 1.0, small), small, h, false);
    std::snprintf(label, sizeof(label), "2D %zu^2 translated dyadic", static_cast<std::size_t>(n));
    reportTranslation("C4a", label, quad(n, 1.0, Vector3{}), quad(n, 1.0, dyadic), dyadic, h, false);
  }
  {
    const auto q16 = mesh::MeshGeometry::createStructuredQuad2D(16, 16, distorted(16, Vector3{}));
    const auto q16b = mesh::MeshGeometry::createStructuredQuad2D(16, 16, distorted(16, small));
    reportTranslation("C4a", "2D Q16 distorted translated small", q16, q16b, small, 1.0 / 16.0,
                      false);
  }

  // ---- C6: scale invariance. rho = Delta/E per field, per scale; the spread across scales
  // (excluding exact zeros) must not exceed 100x, evaluated per field.
  {
    std::vector<std::vector<Real>> perScale;
    for (const Real length : {1e-3, 1.0, 1e3}) {
      char label[96];
      std::snprintf(label, sizeof(label), "2D 16^2 L=%g translated", length);
      const Vector3 offset{0.005 * length, 0.0025 * length, 0.0};
      perScale.push_back(reportTranslation("C6", label, quad(16, length, Vector3{}),
                                           quad(16, length, offset), offset, length / 16.0, false));
    }
    const char* names[] = {"constant", "linear", "quadratic"};
    for (std::size_t f = 0; f < 3; ++f) {
      Real lo = 1e300;
      Real hi = 0.0;
      for (const auto& scale : perScale) {
        if (scale[f] <= 0.0) continue;  // exact zeros excluded by the criterion
        lo = std::min(lo, scale[f]);
        hi = std::max(hi, scale[f]);
      }
      const Real spread = (hi > 0.0 && lo < 1e300) ? hi / lo : 1.0;
      std::printf("C6s %-38s %-9s rho spread %.3e bound 1.000e+02 %s\n", "2D 16^2 across L",
                  names[f], spread, spread <= 100.0 ? "PASS" : "FAIL");
    }
  }

  // ---- C4b: extreme-coordinate diagnostic (no pass/fail) ----
  for (const Index n : {16u, 64u, 256u}) {
    char label[96];
    std::snprintf(label, sizeof(label), "2D %zu^2 translated LARGE", static_cast<std::size_t>(n));
    reportTranslation("C4b", label, quad(n, 1.0, Vector3{}), quad(n, 1.0, large), large,
                      1.0 / static_cast<Real>(n), false);
  }
  std::printf("# C4b sweep: where createStructuredQuad2D geometry leaves the 1e-6 validity domain "
              "(16^2)\n");
  // Non-representable mantissa at every magnitude: powers of ten alone are exactly representable
  // (and i/16 is dyadic), so a decade sweep of round offsets measures nothing -- the first version
  // of this sweep reported "valid" everywhere for that reason (a1/dryrun.md).
  for (const Real decade : {1e-2, 1e-1, 1.0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6}) {
    const Real offset = 1.2345678 * decade;
    const Vector3 o{offset, offset * 0.5, 0.0};
    const Geometry geo =
        translationGeometry(quad(16, 1.0, Vector3{}), quad(16, 1.0, o), o, 1.0 / 16.0);
    std::printf("C4b sweep offset %-9.3e X/h %.3e | volume %.3e centroid %.3e area %.3e -> %s\n",
                offset, (offset + 1.0) * 16.0 / 2.0, geo.volume, geo.centroid, geo.areaVector,
                geo.valid ? "valid" : "INVALID");
  }
  return 0;
}
