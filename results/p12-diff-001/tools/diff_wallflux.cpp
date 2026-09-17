// P12-DIFF-001 step 1-2: reproduce the first-order wall-flux behaviour and DRY-RUN the proposed
// correction against the unchanged baseline, BEFORE freezing any gate or touching production.
//
// Existing production formulation (NonOrthogonalDiffusion.cpp, boundaryFaceDiffusionTerms):
//     flux into owner = coefficient (phi_b - phi_P) + explicitFlux
//     coefficient  = Gamma |S_orth| / distance,  distance = |d| passed by the callers
//     explicitFlux = Gamma S_nonorth . grad(phi)_P
//     S_orth = (S.S)/(d.S) d,  S_nonorth = S - S_orth,  d = x_f - x_P
// Note |S_orth|/|d| = |S|^2/(d.S), so the implicit part is exactly the over-relaxed decomposition's
// d-direction term and the whole expression is ALGEBRAICALLY EXACT for a linear field.
//
// Variants measured here (probe only, production untouched):
//   A  as-production
//   B  normal-distance + tangential transfer of the prescribed value to the normal foot:
//        phi_at_normal_foot = phi_b - grad(phi)_P . d_t     (sign derived below)
//        flux = Gamma |S| (phi_foot - phi_P) / d_n
//   C  the stated sign (+ d_t) in the same normal-difference form, for comparison
//   D  the exact surface integral Gamma grad(phi)_exact(x_f) . (-S) -- the true answer
//
// Fields: constant, linear (both must be exact for any correct formulation) and the parabolic
// Poiseuille profile (the curved field whose behaviour is in question).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using namespace cfd;

namespace {

constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kViscosity = 0.1;
const Real kPi = std::acos(-1.0);

std::vector<Vector2> vertices(Index nx, Index ny, Real distort) {
  const Real ax = 0.1 * distort, ay = 0.05 * distort, lambda = 1.0;
  std::vector<Vector2> v;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = kLength * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = kHeight * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / kLength) * std::sin(2.0 * kPi * eta / kHeight));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / kHeight));
      if (i == 0) x = 0.0;
      if (i == nx) x = kLength;
      if (j == 0) y = 0.0;
      if (j == ny) y = kHeight;
      v.push_back(Vector2{x, y});
    }
  }
  return v;
}

enum class Kind { Constant, Linear, Parabolic };

Real fieldValue(Kind k, const Vector3& x) {
  switch (k) {
    case Kind::Constant:
      return 2.5;
    case Kind::Linear:
      return 0.7 + (1.3 * x.x) - (0.9 * x.y);
    default:
      return 6.0 * (x.y / kHeight) * (1.0 - (x.y / kHeight));
  }
}

Vector3 fieldGradient(Kind k, const Vector3& x) {
  switch (k) {
    case Kind::Constant:
      return Vector3{};
    case Kind::Linear:
      return Vector3{1.3, -0.9, 0.0};
    default:
      return Vector3{0.0, 6.0 * ((1.0 / kHeight) - (2.0 * x.y / (kHeight * kHeight))), 0.0};
  }
}

const char* kindName(Kind k) {
  return k == Kind::Constant ? "constant" : (k == Kind::Linear ? "linear" : "parabolic");
}

struct Totals {
  Real production{0.0};
  Real normalFoot{0.0};
  Real plusTangent{0.0};
  Real exact{0.0};
  Real scale{0.0};  // Gamma |grad phi| A summed over the wall: a non-vanishing physical scale
};

Totals wallFlux(const mesh::Mesh& m, Kind kind) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& c : m.cells()) phi[c.id()] = fieldValue(kind, c.centroid());
  boundary::BoundaryConditionSet bc;
  bc.set(m, "bottom", std::make_unique<boundary::FixedValue>(fieldValue(kind, Vector3{0, 0, 0})));
  bc.set(m, "top",
         std::make_unique<boundary::FixedValue>(fieldValue(kind, Vector3{0, kHeight, 0})));
  bc.set(m, "left", std::make_unique<boundary::FixedGradient>(
                        -fieldGradient(kind, Vector3{0, 0.5, 0}).x));
  bc.set(m, "right", std::make_unique<boundary::FixedGradient>(
                         fieldGradient(kind, Vector3{kLength, 0.5, 0}).x));
  const auto grad =
      discretization::gradient(m, phi, bc, discretization::GradientScheme::GreenGauss);

  Totals t;
  for (const char* wall : {"bottom", "top"}) {
    for (const Index faceId : m.boundaryPatch(wall).faceIds()) {
      const auto& face = m.face(faceId);
      const auto& owner = m.cell(face.owner());
      const Vector3 d = face.centroid() - owner.centroid();
      const Vector3 n = face.areaVector() * (1.0 / face.area());
      const Real dn = dot(d, n);
      const Vector3 dt = d - (n * dn);
      const Real phiB = fieldValue(kind, face.centroid());
      const Real phiP = phi[owner.id()];
      const Vector3 gP = grad[owner.id()];

      const auto terms = discretization::boundaryFaceDiffusionTerms(m, face, kViscosity,
                                                                    magnitude(d), &grad, true);
      t.production += (terms.coefficient * (phiB - phiP)) + terms.explicitFlux;

      // x_foot - x_f = -dt, so phi at the normal foot is phi_b - g . dt.
      t.normalFoot += kViscosity * face.area() * ((phiB - dot(gP, dt)) - phiP) / dn;
      t.plusTangent += kViscosity * face.area() * ((phiB + dot(gP, dt)) - phiP) / dn;
      // The diffusion term's contribution to the owner's balance is Gamma grad(phi) . S_out,
      // which is what coefficient (phi_b - phi_P) + explicitFlux approximates (negative at a wall
      // where phi decreases outward) -- verified against the production numbers below.
      t.exact += kViscosity * dot(fieldGradient(kind, face.centroid()), face.areaVector());
      t.scale += kViscosity * magnitude(fieldGradient(kind, face.centroid())) * face.area();
    }
  }
  return t;
}

void study(Real distort) {
  std::printf("# distortion %.2f\n", distort);
  for (const Kind kind : {Kind::Constant, Kind::Linear, Kind::Parabolic}) {
    std::vector<Real> errA, errB, errC;
    for (const auto& [nx, ny] :
         std::vector<std::pair<Index, Index>>{{64, 8}, {96, 12}, {144, 18}}) {
      const mesh::Mesh m =
          mesh::MeshGeometry::createStructuredQuad2D(nx, ny, vertices(nx, ny, distort));
      const Totals t = wallFlux(m, kind);
      // Normalised by the physical scale, not by the exact total (which vanishes for the constant
      // and linear fields, where the wall fluxes cancel between the two walls).
      const Real scale = (t.scale > 0.0) ? t.scale : 1.0;
      errA.push_back(std::abs(t.production - t.exact) / scale);
      errB.push_back(std::abs(t.normalFoot - t.exact) / scale);
      errC.push_back(std::abs(t.plusTangent - t.exact) / scale);
      const auto q = mesh::MeshQuality::evaluate(m);
      std::printf("F   %-9s %3zux%-3zu nonorth %5.2f | exact %.6e | production %.4e | "
                  "normal-foot %.4e | plus-tangent %.4e | 0.5h = %.4e\n",
                  kindName(kind), static_cast<std::size_t>(nx), static_cast<std::size_t>(ny),
                  q.maxNonOrthogonalityDegrees, t.exact, errA.back(), errB.back(), errC.back(),
                  0.5 * kHeight / static_cast<Real>(ny));
    }
    const auto order = [](Real coarse, Real fine) {
      return (coarse > 0.0 && fine > 0.0) ? std::log(coarse / fine) / std::log(1.5) : 0.0;
    };
    std::printf("O   %-9s observed order: production %.3f / %.3f | normal-foot %.3f / %.3f | "
                "plus-tangent %.3f / %.3f\n",
                kindName(kind), order(errA[0], errA[1]), order(errA[1], errA[2]),
                order(errB[0], errB[1]), order(errB[1], errB[2]), order(errC[0], errC[1]),
                order(errC[1], errC[2]));
  }
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-001 wall-flux dry run: is the first-order error a non-orthogonality "
              "defect?\n");
  study(0.0);
  study(1.0);
  return 0;
}
