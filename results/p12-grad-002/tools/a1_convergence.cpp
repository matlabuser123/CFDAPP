// P12-GRAD-002 A1: C3(b) Cartesian convergence, non-orthogonal convergence, and 3D verification.
// Runs against base and new alike (pre-MESH-007 APIs only).
//
// Field: phi = x^3, whose boundary conditions are EXACTLY representable per patch on any mesh whose
// domain is the unit box (FixedValue 0 and 1 on the two constant-x patches, FixedGradient(0) on the
// others, since dphi/dn = 0 there), and which is beyond the boundary reconstruction's exactness
// (that is exact for quadratics). So the observed order includes boundary-adjacent cells with no
// boundary-condition error contaminating it -- which is the whole point of the criterion.
// grad phi = (3x^2, 0, 0).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

Real value(const Vector3& x, Real x0) { return (x.x - x0) * (x.x - x0) * (x.x - x0); }
Vector3 exactGradient(const Vector3& x, Real x0) {
  return Vector3{3.0 * (x.x - x0) * (x.x - x0), 0.0, 0.0};
}

boundary::BoundaryConditionSet conditions(const mesh::Mesh& m, Real x0, bool& exact) {
  boundary::BoundaryConditionSet set;
  exact = true;
  for (const auto& patch : m.boundaryPatches()) {
    const auto& first = m.face(patch.faceIds().front());
    const Vector3 n0 = first.areaVector() * (1.0 / first.area());
    const bool constantX = std::abs(std::abs(n0.x) - 1.0) < 1e-12;
    for (const Index id : patch.faceIds()) {
      const auto& face = m.face(id);
      const Vector3 n = face.areaVector() * (1.0 / face.area());
      if (magnitude(n - n0) > 1e-12) exact = false;
      if (constantX) {
        if (std::abs(face.centroid().x - first.centroid().x) >
            1e-12 * std::max(1.0, std::abs(first.centroid().x))) {
          exact = false;
        }
      } else if (std::abs(n.x) > 1e-12) {
        exact = false;
      }
    }
    if (constantX) {
      set.set(m, patch.name(),
              std::make_unique<boundary::FixedValue>(value(first.centroid(), x0)));
    } else {
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return set;
}

struct Errors {
  Real linf{0.0};
  Real l2{0.0};
  Real linfBoundary{0.0};
  bool exact{false};
};

Errors measure(const mesh::Mesh& m, Real x0) {
  Errors e;
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& c : m.cells()) phi[c.id()] = value(c.centroid(), x0);
  const auto bc = conditions(m, x0, e.exact);
  const auto g =
      discretization::gradient(m, phi, bc, discretization::GradientScheme::GreenGauss);
  Real sum = 0.0;
  for (const auto& c : m.cells()) {
    const Real err = magnitude(g[c.id()] - exactGradient(c.centroid(), x0));
    e.linf = std::max(e.linf, err);
    sum += err * err;
    bool boundary = false;
    for (const Index id : c.faceIds()) {
      if (m.face(id).isBoundary()) boundary = true;
    }
    if (boundary) e.linfBoundary = std::max(e.linfBoundary, err);
  }
  e.l2 = std::sqrt(sum / static_cast<Real>(m.numberOfCells()));
  return e;
}

Real order(Real coarse, Real fine) {
  if (!(coarse > 0.0) || !(fine > 0.0)) return 0.0;
  return std::log(coarse / fine) / std::log(2.0);
}

std::vector<Vector3> cartesian(Index n, const Vector3& o) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n)) + o.x,
                          (static_cast<Real>(j) / static_cast<Real>(n)) + o.y, 0.0});
    }
  }
  return v;
}

// Distorted family: interior vertices displaced, domain boundary exactly the unit square, so the
// patches stay flat and axis-aligned and the boundary values stay exact at every resolution.
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

void family(const char* label, const std::vector<Index>& levels,
            const std::function<mesh::Mesh(Index)>& build, Real x0) {
  std::vector<Errors> errors;
  for (const Index n : levels) errors.push_back(measure(build(n), x0));
  for (std::size_t i = 0; i < levels.size(); ++i) {
    const std::string suffix =
        (i == 0) ? std::string("  (reference level)")
                 : (" order Linf " + std::to_string(order(errors[i - 1].linf, errors[i].linf)) +
                    " L2 " + std::to_string(order(errors[i - 1].l2, errors[i].l2)) + " boundary " +
                    std::to_string(order(errors[i - 1].linfBoundary, errors[i].linfBoundary)));
    std::printf("CV  %-32s n %4zu Linf %.3e L2 %.3e boundary %.3e%s%s\n", label,
                static_cast<std::size_t>(levels[i]), errors[i].linf, errors[i].l2,
                errors[i].linfBoundary, suffix.c_str(),
                errors[i].exact ? "" : " [BC NOT EXACT -- INVALID]");
  }
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 A1: convergence of grad(x^3), exact per-patch boundary values\n");
  const Vector3 small{0.005, 0.0025, 0.0};
  family("2D Cartesian plain", {16, 32, 64, 128},
         [](Index n) { return mesh::MeshGeometry::createStructuredQuad2D(n, n, cartesian(n, Vector3{})); },
         0.0);
  family("2D Cartesian translated small", {16, 32, 64, 128},
         [&](Index n) { return mesh::MeshGeometry::createStructuredQuad2D(n, n, cartesian(n, small)); },
         small.x);
  family("2D distorted (non-orthogonal)", {16, 32, 64, 128},
         [](Index n) { return mesh::MeshGeometry::createStructuredQuad2D(n, n, distorted(n, Vector3{})); },
         0.0);
  family("2D distorted translated small", {16, 32, 64, 128},
         [&](Index n) { return mesh::MeshGeometry::createStructuredQuad2D(n, n, distorted(n, small)); },
         small.x);
  family("3D Cartesian plain", {8, 16, 32},
         [](Index n) { return mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0); }, 0.0);
  return 0;
}
