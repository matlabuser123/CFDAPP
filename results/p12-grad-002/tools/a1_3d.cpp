// P12-GRAD-002 A1, criterion C9: 3D translation and deformation robustness, against the same
// frozen A1 envelope E = eps*8*Phi*G + 100*C_g*|grad phi|_ref.
//
// The only offset-capable 3D path in this codebase is P12-MESH-007's mesh-motion API (createCartesian3D
// takes no origin), which the pre-MESH-007 baseline does not have -- so this tool is new-library only,
// and that limitation is reported rather than hidden. The motion is used purely as a geometry
// translation/deformation; no ALE solver is involved.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <limits>
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
constexpr Real kFace = 8.0;
constexpr Real kGeom = 100.0;
constexpr Real kGeometryTolerance = 1e-6;

enum class Kind { Constant, Linear, Quadratic };

Real fieldValue(Kind k, const Vector3& x, const Vector3& o) {
  switch (k) {
    case Kind::Constant:
      return 2.5;
    case Kind::Linear:
      return 0.7 + (1.3 * (x.x - o.x)) - (0.9 * (x.y - o.y)) + (0.4 * (x.z - o.z));
    case Kind::Quadratic:
    default:
      return 0.5 * (x.x - o.x) * (x.x - o.x);
  }
}

Vector3 fieldGradient(Kind k, const Vector3& x, const Vector3& o) {
  switch (k) {
    case Kind::Constant:
      return Vector3{};
    case Kind::Linear:
      return Vector3{1.3, -0.9, 0.4};
    case Kind::Quadratic:
    default:
      return Vector3{x.x - o.x, 0.0, 0.0};
  }
}

// Exact per-patch conditions, as elsewhere: FixedValue on planes of constant x for the axial
// quadratic, FixedGradient otherwise; `exact` false if any face is not exactly represented.
boundary::BoundaryConditionSet conditionsFor(const mesh::Mesh& m, Kind k, const Vector3& o,
                                             bool& exact) {
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
    if (k == Kind::Constant) {
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
      continue;
    }
    if (k == Kind::Linear) {
      if (!flat) exact = false;
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(
                                   dot(fieldGradient(k, first.centroid(), o), n0)));
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
      set.set(m, patch.name(),
              std::make_unique<boundary::FixedValue>(fieldValue(k, Vector3{x, 0, 0}, o)));
    } else {
      if (!(flat && std::abs(n0.x) <= 1e-12)) exact = false;
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return set;
}

fields::VectorField gradientOf(const mesh::Mesh& m, Kind k, const Vector3& o, bool& exact) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& c : m.cells()) phi[c.id()] = fieldValue(k, c.centroid(), o);
  const auto bc = conditionsFor(m, k, o, exact);
  return discretization::gradient(m, phi, bc, discretization::GradientScheme::GreenGauss);
}

Real conditioning(const mesh::Mesh& m) {
  Real worst = 0.0;
  for (const auto& c : m.cells()) {
    Real area = 0.0;
    for (const Index id : c.faceIds()) area += m.face(id).area();
    worst = std::max(worst, area / c.volume());
  }
  return worst;
}

Real envelopeFor(const mesh::Mesh& m, Kind k, const Vector3& o, Real cg) {
  Real phi = 0.0;
  Real grad = 0.0;
  for (const auto& c : m.cells()) {
    phi = std::max(phi, std::abs(fieldValue(k, c.centroid(), o)));
    grad = std::max(grad, magnitude(fieldGradient(k, c.centroid(), o)));
  }
  for (const auto& f : m.faces()) {
    if (f.isBoundary()) phi = std::max(phi, std::abs(fieldValue(k, f.centroid(), o)));
  }
  return (kEps * kFace * phi * conditioning(m)) + (kGeom * cg * grad);
}

mesh::Mesh translatedBox(Index n, const Vector3& offset) {
  mesh::Mesh m = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  mesh::MeshMotion motion(
      m, std::make_shared<mesh::AffineMotion>(mesh::AffineMotion::Matrix{}, Vector3{}, offset));
  (void)motion.advance(1.0);
  return m;
}

const char* kindName(Kind k) {
  return k == Kind::Constant ? "constant" : (k == Kind::Linear ? "linear" : "quadratic");
}

void translation(const char* label, Index n, const Vector3& offset) {
  const mesh::Mesh a = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  const mesh::Mesh b = translatedBox(n, offset);
  const Real h = 1.0 / static_cast<Real>(n);
  Real cg = 0.0;
  Real volume = 0.0;
  for (const auto& c : a.cells()) {
    const auto& d = b.cell(c.id());
    cg = std::max(cg, magnitude(d.centroid() - offset - c.centroid()) / h);
    volume = std::max(volume, std::abs(d.volume() - c.volume()) / c.volume());
  }
  std::printf("V3D %-34s volume %.3e centroid %.3e -> %s\n", label, volume, cg,
              (cg <= kGeometryTolerance && volume <= kGeometryTolerance) ? "VALID GEOMETRY"
                                                                        : "INVALID GEOMETRY");
  for (const Kind k : {Kind::Constant, Kind::Linear, Kind::Quadratic}) {
    bool ea = false;
    bool eb = false;
    const auto ga = gradientOf(a, k, Vector3{}, ea);
    const auto gb = gradientOf(b, k, offset, eb);
    Real delta = 0.0;
    for (Index i = 0; i < ga.size(); ++i) delta = std::max(delta, magnitude(ga[i] - gb[i]));
    const Real e = envelopeFor(a, k, Vector3{}, cg);
    std::printf("C9  %-34s %-9s delta %.3e E %.3e ratio %.3e %s%s\n", label, kindName(k), delta, e,
                (e > 0.0) ? delta / e : 0.0, delta <= e ? "PASS" : "FAIL",
                (ea && eb) ? "" : " [BC NOT EXACT -- INVALID]");
  }
}

// Deformed 3D mesh: constant and linear fields must still be reproduced (the deformation keeps the
// outer boundary fixed, so the patches stay flat and the prescribed values stay exact).
void deformation(const char* label, Index n, Real amplitude) {
  mesh::Mesh m = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  mesh::MeshMotion motion(
      m, std::make_shared<mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                  Vector3{amplitude, 0.5 * amplitude,
                                                          -0.75 * amplitude},
                                                  constants::twoPi / 0.4));
  (void)motion.advance(0.1);
  Real maxSine = 0.0;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    const Vector3 d = face.centroid() - m.cell(face.owner()).centroid();
    const Real dm = magnitude(d);
    const Real sm = magnitude(face.areaVector());
    if (dm > 0.0 && sm > 0.0) maxSine = std::max(maxSine, magnitude(cross(d, face.areaVector())) / (dm * sm));
  }
  for (const Kind k : {Kind::Constant, Kind::Linear}) {
    bool exact = false;
    const auto g = gradientOf(m, k, Vector3{}, exact);
    Real delta = 0.0;
    for (const auto& c : m.cells()) {
      delta = std::max(delta, magnitude(g[c.id()] - fieldGradient(k, c.centroid(), Vector3{})));
    }
    const Real e = envelopeFor(m, k, Vector3{}, 0.0);
    std::printf("C9  %-34s %-9s delta %.3e E(floor) %.3e | max m_f %.3e %s%s\n", label, kindName(k),
                delta, e, maxSine, delta <= std::max(e, 1e-9) ? "PASS" : "FAIL",
                exact ? "" : " [BC NOT EXACT]");
  }
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 A1 C9: 3D translation and deformation (new library only -- the "
              "baseline has no offset-capable 3D path)\n");
  translation("3D 8^3 translated small", 8, Vector3{0.005, 0.0025, 0.001});
  translation("3D 16^3 translated small", 16, Vector3{0.005, 0.0025, 0.001});
  translation("3D 8^3 translated dyadic", 8, Vector3{1.0 / 128.0, 1.0 / 256.0, 1.0 / 512.0});
  translation("3D 8^3 translated LARGE", 8, Vector3{1234.5678, 987.6543, 543.21});
  deformation("3D 8^3 deformed", 8, 0.05);
  deformation("3D 16^3 deformed", 16, 0.05);
  return 0;
}
