// P12-GRAD-002 gradient probe: criteria C1-C4, C6 and the stencil diagnostics, measured through
// public APIs that exist in every library version, so the SAME program runs against base
// (pre-MESH-007, exact-zero predicate), grad001 (hard tolerance) and new (GRAD-002).
//
//   T  translation / scale invariance (C1, C4, C6): the Green-Gauss gradient of an analytic field on
//      a mesh against the same mesh translated by a given offset, cell by cell, with the same field
//      values and the same boundary conditions on both. Reported for interior and boundary-adjacent
//      cells separately, with X/h and the frozen bound max(1e-13, 200 eps (X/h)).
//   A  analytic accuracy (C1, C2, C3a): max/L2 error against the analytic gradient.
//   G  stencil diagnostics: max normalized misalignment m_f, the anti-parallel margin of the
//      opposite-face choice, and the number of interior faces claimed by two boundary faces.
//
// Boundary conditions are chosen so that every reported "all cells" number has EXACT boundary
// values, which is what lets boundary cells be verified at all:
//   constant  -> FixedGradient(0) on every patch (exact on any patch, flat or not);
//   linear    -> FixedGradient(g . n) per patch (exact iff the patch is flat);
//   quadratic -> phi = (x - x0)^2 / 2, axial: FixedValue on the two x-patches (constant along them)
//                and FixedGradient(0) elsewhere (exact iff the x-patches are planes of constant x
//                and the others have normals perpendicular to x).
// Where a field's boundary values cannot be exact on a mesh, only interior cells are compared and
// the row says so.
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

constexpr Real kEpsilon = std::numeric_limits<Real>::epsilon();

enum class Kind { Constant, Linear, AxialQuadratic };

struct Analytic {
  std::string name;
  Kind kind{Kind::Constant};
  Vector3 origin{};  // AxialQuadratic: phi = (x.x - origin.x)^2 / 2
  std::function<Real(const Vector3&)> value;
  std::function<Vector3(const Vector3&)> gradient;
};

Analytic makeField(Kind kind, bool threeD, const Vector3& origin) {
  switch (kind) {
    case Kind::Constant:
      return {"constant", kind, origin, [](const Vector3&) { return 2.5; },
              [](const Vector3&) { return Vector3{}; }};
    case Kind::Linear:
      if (threeD) {
        return {"linear", kind, origin,
                [origin](const Vector3& x) {
                  const Vector3 r = x - origin;
                  return 0.7 + (1.3 * r.x) - (0.9 * r.y) + (0.4 * r.z);
                },
                [](const Vector3&) { return Vector3{1.3, -0.9, 0.4}; }};
      }
      return {"linear", kind, origin,
              [origin](const Vector3& x) {
                const Vector3 r = x - origin;
                return 0.7 + (1.3 * r.x) - (0.9 * r.y);
              },
              [](const Vector3&) { return Vector3{1.3, -0.9, 0.0}; }};
    case Kind::AxialQuadratic:
    default:
      return {"quadratic", kind, origin,
              [origin](const Vector3& x) { return 0.5 * (x.x - origin.x) * (x.x - origin.x); },
              [origin](const Vector3& x) { return Vector3{x.x - origin.x, 0.0, 0.0}; }};
  }
}

std::vector<Analytic> allFields(bool threeD, const Vector3& origin) {
  return {makeField(Kind::Constant, threeD, origin), makeField(Kind::Linear, threeD, origin),
          makeField(Kind::AxialQuadratic, threeD, origin)};
}

bool isInterior(const mesh::Mesh& m, const mesh::Cell& cell) {
  for (const Index f : cell.faceIds()) {
    if (m.face(f).isBoundary()) return false;
  }
  return true;
}

struct PatchNormal {
  Vector3 normal{};
  bool flat{true};
};

PatchNormal patchNormal(const mesh::Mesh& m, const mesh::BoundaryPatch& patch) {
  PatchNormal result;
  bool first = true;
  for (const Index f : patch.faceIds()) {
    const Vector3 n = m.face(f).areaVector() * (1.0 / m.face(f).area());
    if (first) {
      result.normal = n;
      first = false;
    } else if (magnitude(n - result.normal) > 1e-12) {
      result.flat = false;
    }
  }
  return result;
}

// Is every face of this patch a plane of constant x (normal exactly +/-x, so a
// value prescribed on it is the field's own constant value there)?
bool isConstantXPatch(const mesh::Mesh& m, const mesh::BoundaryPatch& patch, Real& xValue) {
  const PatchNormal n = patchNormal(m, patch);
  if (!n.flat || std::abs(std::abs(n.normal.x) - 1.0) > 1e-12) return false;
  xValue = m.face(patch.faceIds().front()).centroid().x;
  for (const Index f : patch.faceIds()) {
    if (std::abs(m.face(f).centroid().x - xValue) > 1e-12 * std::max(1.0, std::abs(xValue))) {
      return false;
    }
  }
  return true;
}

boundary::BoundaryConditionSet conditionsFor(const mesh::Mesh& m, const Analytic& f, bool& exact) {
  boundary::BoundaryConditionSet set;
  exact = true;
  for (const auto& patch : m.boundaryPatches()) {
    const PatchNormal n = patchNormal(m, patch);
    if (f.kind == Kind::Constant) {
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
      continue;
    }
    if (f.kind == Kind::Linear) {
      if (!n.flat) exact = false;
      set.set(m, patch.name(),
              std::make_unique<boundary::FixedGradient>(
                  dot(f.gradient(m.face(patch.faceIds().front()).centroid()), n.normal)));
      continue;
    }
    Real xValue = 0.0;
    if (isConstantXPatch(m, patch, xValue)) {
      set.set(m, patch.name(), std::make_unique<boundary::FixedValue>(f.value(Vector3{xValue, 0, 0})));
    } else if (n.flat && std::abs(n.normal.x) <= 1e-12) {
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    } else {
      exact = false;
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return set;
}

fields::ScalarField sample(const mesh::Mesh& m, const Analytic& f) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& cell : m.cells()) phi[cell.id()] = f.value(cell.centroid());
  return phi;
}

Real gradientScale(const mesh::Mesh& m, const Analytic& f) {
  Real scale = 0.0;
  for (const auto& cell : m.cells()) {
    scale = std::max(scale, magnitude(f.gradient(cell.centroid())));
  }
  return scale > 0.0 ? scale : 1.0;
}

// X/h in GRAD-001's own definition, so the two phases' numbers are comparable:
// per boundary face, the largest absolute coordinate of (owner centroid, face
// centroid) over twice the owner-to-face distance; reported as the maximum.
Real maxSizeRatio(const mesh::Mesh& m) {
  Real ratio = 0.0;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    const Vector3& p = m.cell(face.owner()).centroid();
    const Vector3& f = face.centroid();
    const Real d = magnitude(f - p);
    if (!(d > 0.0)) continue;
    const Real x = std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z), std::abs(f.x),
                             std::abs(f.y), std::abs(f.z)});
    ratio = std::max(ratio, x / (2.0 * d));
  }
  return ratio;
}

Real frozenTranslationBound(const mesh::Mesh& m) {
  return std::max(1e-13, 200.0 * kEpsilon * maxSizeRatio(m));
}

void reportInvariance(const char* label, const mesh::Mesh& base, const mesh::Mesh& moved,
                      const Vector3& offset, bool threeD) {
  const Real bound = std::max(frozenTranslationBound(base), frozenTranslationBound(moved));
  for (const auto& f : allFields(threeD, Vector3{})) {
    const Analytic shifted = makeField(f.kind, threeD, offset);
    bool exactA = false;
    bool exactB = false;
    const auto bcA = conditionsFor(base, f, exactA);
    const auto bcB = conditionsFor(moved, shifted, exactB);
    const auto gA = discretization::gradient(base, sample(base, f), bcA,
                                             discretization::GradientScheme::GreenGauss);
    const auto gB = discretization::gradient(moved, sample(moved, shifted), bcB,
                                             discretization::GradientScheme::GreenGauss);
    const Real scale = gradientScale(base, f);
    Real interior = 0.0;
    Real boundary = 0.0;
    for (const auto& cell : base.cells()) {
      const Real d = magnitude(gA[cell.id()] - gB[cell.id()]) / scale;
      if (isInterior(base, cell)) {
        interior = std::max(interior, d);
      } else {
        boundary = std::max(boundary, d);
      }
    }
    const Real worst = std::max(interior, boundary);
    std::printf("T %-40s %-10s interior %.3e boundary %.3e | X/h %.3e bound %.3e %s%s\n", label,
                f.name.c_str(), interior, boundary, maxSizeRatio(moved), bound,
                worst <= bound ? "PASS" : "FAIL", (exactA && exactB) ? "" : " [interior only]");
  }
}

void reportAnalytic(const char* label, const mesh::Mesh& m, bool threeD) {
  for (const auto& f : allFields(threeD, Vector3{})) {
    bool exact = false;
    const auto bc = conditionsFor(m, f, exact);
    const auto g =
        discretization::gradient(m, sample(m, f), bc, discretization::GradientScheme::GreenGauss);
    const Real scale = gradientScale(m, f);
    Real linfAll = 0.0;
    Real linfInterior = 0.0;
    Real l2 = 0.0;
    Index cells = 0;
    for (const auto& cell : m.cells()) {
      const Real e = magnitude(g[cell.id()] - f.gradient(cell.centroid())) / scale;
      linfAll = std::max(linfAll, e);
      if (isInterior(m, cell)) linfInterior = std::max(linfInterior, e);
      l2 += e * e;
      ++cells;
    }
    std::printf("A %-40s %-10s interior Linf %.3e | all cells Linf %.3e L2 %.3e (%s)\n", label,
                f.name.c_str(), linfInterior, linfAll,
                std::sqrt(l2 / static_cast<Real>(cells)),
                exact ? "boundary values exact" : "boundary values NOT exact patch-wise");
  }
}

// Stencil diagnostics -- reported, not gated (the GRAD-001 trap was gating on
// branch classification). Uses only APIs present in every library version.
void reportStencil(const char* label, const mesh::Mesh& m) {
  Real maxSine = 0.0;
  Real worstMargin = 1.0;  // closest the opposite-face choice comes to a tie
  Index doubleClaimed = 0;
  Index withoutOpposite = 0;
  for (const auto& cell : m.cells()) {
    std::vector<Index> claimed;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = m.face(faceId);
      if (!face.isBoundary()) continue;
      const Vector3 d = face.centroid() - cell.centroid();
      const Vector3& sf = face.areaVector();
      const Real dm = magnitude(d);
      const Real sm = magnitude(sf);
      if (dm > 0.0 && sm > 0.0) maxSine = std::max(maxSine, magnitude(cross(d, sf)) / (dm * sm));
      const auto opposite = mesh::MeshGeometry::oppositeInteriorFace(m, cell, face);
      if (!opposite.has_value()) {
        ++withoutOpposite;
        continue;
      }
      if (std::find(claimed.begin(), claimed.end(), *opposite) != claimed.end()) ++doubleClaimed;
      claimed.push_back(*opposite);
      // Margin: how much more anti-parallel the winner is than the runner-up.
      const Vector3 reference = mesh::MeshGeometry::unitNormal(face);
      Real best = 1.0;
      Real second = 1.0;
      for (const Index candidateId : cell.faceIds()) {
        if (candidateId == faceId) continue;
        const auto& candidate = m.face(candidateId);
        if (candidate.isBoundary()) continue;
        const Vector3 n = (candidate.owner() == cell.id())
                              ? mesh::MeshGeometry::unitNormal(candidate)
                              : (mesh::MeshGeometry::unitNormal(candidate) * -1.0);
        const Real alignment = dot(reference, n);
        if (alignment < best) {
          second = best;
          best = alignment;
        } else if (alignment < second) {
          second = alignment;
        }
      }
      worstMargin = std::min(worstMargin, second - best);
    }
  }
  std::printf("G %-40s max m_f %.3e | opposite-face margin >= %.3f | double-claimed %zu | "
              "no-opposite %zu\n",
              label, maxSine, worstMargin, static_cast<std::size_t>(doubleClaimed),
              static_cast<std::size_t>(withoutOpposite));
}

std::vector<Vector3> cartesianVertices(Index n, Real length, const Vector3& offset) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n) * length) + offset.x,
                          (static_cast<Real>(j) / static_cast<Real>(n) * length) + offset.y, 0.0});
    }
  }
  return v;
}

// Q16: the interior vertices move, the domain boundary stays exactly the unit
// square (both perturbations vanish on every edge), so the patches are flat and
// axis-aligned while d is genuinely non-parallel to S_f -- the mesh that
// diagnosed MESH-007 G6.3.
std::vector<Vector3> distortedVertices(Index n, const Vector3& offset) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real x = static_cast<Real>(i) / static_cast<Real>(n);
      const Real y = static_cast<Real>(j) / static_cast<Real>(n);
      const Real pi = constants::pi;
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)) + offset.x,
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)) + offset.y, 0.0});
    }
  }
  return v;
}

mesh::Mesh quad(Index n, Real length, const Vector3& offset) {
  return mesh::MeshGeometry::createStructuredQuad2D(n, n, cartesianVertices(n, length, offset));
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 gradient probe\n");
  const Vector3 small{0.005, 0.0025, 0.0};
  const Vector3 large{1234.5678, 987.6543, 0.0};
  const Vector3 dyadic{1.0 / 128.0, 1.0 / 256.0, 0.0};

  for (const Index n : {16u, 32u, 64u, 128u, 256u}) {
    char label[96];
    for (const auto& [name, offset] :
         std::vector<std::pair<const char*, Vector3>>{{"small", small}, {"dyadic", dyadic},
                                                      {"LARGE", large}}) {
      std::snprintf(label, sizeof(label), "2D %zu^2 translated %s", static_cast<std::size_t>(n),
                    name);
      reportInvariance(label, quad(n, 1.0, Vector3{}), quad(n, 1.0, offset), offset, false);
    }
  }
  for (const Real length : {1e-3, 1e3}) {
    const Vector3 offset{0.005 * length, 0.0025 * length, 0.0};
    char label[96];
    std::snprintf(label, sizeof(label), "2D 16^2 L = %g translated", length);
    reportInvariance(label, quad(16, length, Vector3{}), quad(16, length, offset), offset, false);
  }
  {
    const auto q = [](const Vector3& o) {
      return mesh::MeshGeometry::createStructuredQuad2D(16, 16, distortedVertices(16, o));
    };
    reportInvariance("2D Q16 distorted translated small", q(Vector3{}), q(small), small, false);
  }

  reportAnalytic("2D 16^2 exact Cartesian", mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0),
                 false);
  reportAnalytic("2D 16^2 shoelace Cartesian", quad(16, 1.0, Vector3{}), false);
  reportAnalytic("2D 16^2 translated small", quad(16, 1.0, small), false);
  reportAnalytic("2D 16^2 translated LARGE", quad(16, 1.0, large), false);
  reportAnalytic("2D 64^2 translated small", quad(64, 1.0, small), false);
  reportAnalytic("2D 16^2 L = 1e-3", quad(16, 1e-3, Vector3{5e-6, 2.5e-6, 0.0}), false);
  reportAnalytic("2D 16^2 L = 1e3", quad(16, 1e3, Vector3{5.0, 2.5, 0.0}), false);
  reportAnalytic("2D Q16 distorted",
                 mesh::MeshGeometry::createStructuredQuad2D(16, 16, distortedVertices(16, Vector3{})),
                 false);
  reportAnalytic("3D 8^3 exact Cartesian", mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1, 1, 1),
                 true);

  reportStencil("2D 16^2 exact Cartesian", mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  reportStencil("2D 16^2 translated small", quad(16, 1.0, small));
  reportStencil("2D 16^2 translated LARGE", quad(16, 1.0, large));
  reportStencil("2D 256^2 translated small", quad(256, 1.0, small));
  reportStencil("2D Q16 distorted",
                mesh::MeshGeometry::createStructuredQuad2D(16, 16, distortedVertices(16, Vector3{})));
  reportStencil("3D 8^3 exact Cartesian", mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1, 1, 1));
  return 0;
}
