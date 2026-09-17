// P12-GRAD-002-INV-001, root-cause test: is the GRAD-002 gradient CONVERGED in the 4 sweeps the
// production path uses (kGreenGaussSkewCorrectionSweeps = 4)?
//
// The boundary-consistent face value transfers the boundary value along the face with the PREVIOUS
// sweep's gradient, so it is a lagged fixed-point iteration whose gain scales with the boundary
// misalignment. Differentiating the correction gives d(grad)/d(grad) ~ 0.33 m, so on a mesh with
// m = sin(48 deg) ~ 0.74 the gain is ~0.25 and 4 sweeps leave ~0.25^4 ~ 0.4% of the correction
// unconverged -- a solution-dependent inconsistency that the outer SIMPLE iteration then has to
// chase. Q16 (m = 0.18, gain 0.06) converges in 2, which is why the earlier Q16 sweep test showed
// bit-identical results at 4/8/16.
//
// Measured through the PUBLIC greenGaussGradient(mesh, field, bc, sweeps) overload, so nothing is
// modified; the production path itself always passes 4.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using namespace cfd;

namespace {

const Real kPi = std::acos(-1.0);

std::vector<Vector2> poiseuilleVertices(Index nx, Index ny, Real ax, Real ay) {
  const Real L = 8.0, H = 1.0, lambda = 1.0;
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

Real uExact(const Vector3& x) { return 6.0 * x.y * (1.0 - x.y); }

void study(const char* label, const mesh::Mesh& m) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& c : m.cells()) phi[c.id()] = uExact(c.centroid());
  boundary::BoundaryConditionSet bc;
  bc.set(m, "bottom", std::make_unique<boundary::FixedValue>(0.0));
  bc.set(m, "top", std::make_unique<boundary::FixedValue>(0.0));
  bc.set(m, "left", std::make_unique<boundary::FixedGradient>(0.0));
  bc.set(m, "right", std::make_unique<boundary::FixedGradient>(0.0));

  Real maxSine = 0.0;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    const Vector3 d = face.centroid() - m.cell(face.owner()).centroid();
    const Real dm = magnitude(d);
    const Real sm = magnitude(face.areaVector());
    if (dm > 0.0 && sm > 0.0) {
      maxSine = std::max(maxSine, magnitude(cross(d, face.areaVector())) / (dm * sm));
    }
  }
  const auto quality = mesh::MeshQuality::evaluate(m);
  std::printf("# %s: max boundary misalignment sine %.4f (non-orthogonality %.2f deg), predicted "
              "lagged gain ~%.3f\n",
              label, maxSine, quality.maxNonOrthogonalityDegrees, 0.33 * maxSine);

  fields::VectorField previous;
  for (const Index sweeps : {1u, 2u, 4u, 8u, 16u, 32u, 64u}) {
    const auto g = discretization::greenGaussGradient(m, phi, bc, sweeps);
    Real change = 0.0;
    if (!previous.empty()) {
      for (Index i = 0; i < g.size(); ++i) change = std::max(change, magnitude(g[i] - previous[i]));
    }
    Real errorLinf = 0.0;
    for (const auto& c : m.cells()) {
      const Vector3 exact{0.0, 6.0 * (1.0 - (2.0 * c.centroid().y)), 0.0};
      errorLinf = std::max(errorLinf, magnitude(g[c.id()] - exact));
    }
    std::printf("S   %-22s sweeps %2zu | change from previous count %.4e | error vs analytic "
                "Linf %.4e%s\n",
                label, static_cast<std::size_t>(sweeps), change, errorLinf,
                sweeps == 4 ? "   <-- the production path uses 4" : "");
    previous = g;
  }
}

}  // namespace

int main() {
  std::printf("# INV-001 sweep convergence of the boundary-consistent gradient\n");
  study("Poiseuille 144x18", mesh::MeshGeometry::createStructuredQuad2D(
                                 144, 18, poiseuilleVertices(144, 18, 0.1, 0.05)));
  study("Poiseuille 64x8", mesh::MeshGeometry::createStructuredQuad2D(
                               64, 8, poiseuilleVertices(64, 8, 0.1, 0.05)));
  study("undistorted 144x18", mesh::MeshGeometry::createStructuredQuad2D(
                                  144, 18, poiseuilleVertices(144, 18, 0.0, 0.0)));
  return 0;
}
