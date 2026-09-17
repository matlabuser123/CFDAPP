// P12-GRAD-002-INV-001, step 8: does the corrected gradient make the WALL DIFFUSIVE FLUX more or
// less accurate? For fully developed channel flow dp/dx is fixed by the wall force balance, so an
// error in the wall shear appears directly as a dp/dx error -- the observable that regressed.
//
// The exact analytic field is imposed (so this is pure operator error, no solver involved), the
// gradient comes from the linked library, and the wall flux is assembled with the PRODUCTION
// formula boundaryFaceDiffusionTerms():
//     flux into owner = coefficient * (phi_b - phi_P) + explicitFlux,
//     coefficient = mu |S_orth| / |d|,  explicitFlux = mu S_nonorth . grad(phi)_P.
// Exact: mu * (du/dy)|wall * A_f, and (du/dy)|wall = 6 U / H exactly on both walls (the wall faces
// lie exactly on y = 0 and y = H).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kViscosity = 0.1;
const Real kPi = std::acos(-1.0);

std::vector<Vector2> poiseuilleVertices(Index nx, Index ny) {
  const Real ax = 0.1, ay = 0.05, lambda = 1.0;
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

Real uExact(const Vector3& x) {
  return 6.0 * kMeanVelocity * (x.y / kHeight) * (1.0 - (x.y / kHeight));
}

void study(Index nx, Index ny) {
  const mesh::Mesh m = mesh::MeshGeometry::createStructuredQuad2D(nx, ny,
                                                                  poiseuilleVertices(nx, ny));
  fields::ScalarField u(m.numberOfCells());
  for (const auto& c : m.cells()) u[c.id()] = uExact(c.centroid());
  boundary::BoundaryConditionSet bc;
  bc.set(m, "bottom", std::make_unique<boundary::FixedValue>(0.0));
  bc.set(m, "top", std::make_unique<boundary::FixedValue>(0.0));
  bc.set(m, "left", std::make_unique<boundary::FixedGradient>(0.0));
  bc.set(m, "right", std::make_unique<boundary::FixedGradient>(0.0));
  const auto grad =
      discretization::gradient(m, u, bc, discretization::GradientScheme::GreenGauss);

  // Exact d(u)/dy at either wall is 6 U / H; the inward normal is +y at the bottom and -y at the
  // top, so the exact flux into the owner is mu * 6 U / H * A_f on both walls.
  const Real exactPerArea = kViscosity * 6.0 * kMeanVelocity / kHeight;
  Real totalComputed = 0.0;
  Real totalExact = 0.0;
  Real worstRelative = 0.0;
  Real totalExplicit = 0.0;
  Index faces = 0;
  for (const char* wall : {"bottom", "top"}) {
    for (const Index faceId : m.boundaryPatch(wall).faceIds()) {
      const auto& face = m.face(faceId);
      const auto& owner = m.cell(face.owner());
      const Real distance = magnitude(face.centroid() - owner.centroid());
      const auto terms = discretization::boundaryFaceDiffusionTerms(m, face, kViscosity, distance,
                                                                    &grad, true);
      const Real computed = (terms.coefficient * (0.0 - u[owner.id()])) + terms.explicitFlux;
      const Real exact = -exactPerArea * face.area();  // flux into the owner is negative here
      totalComputed += computed;
      totalExact += exact;
      totalExplicit += terms.explicitFlux;
      worstRelative = std::max(worstRelative, std::abs((computed - exact) / exact));
      ++faces;
    }
  }
  std::printf("W   %3zux%-3zu faces %4zu | total wall flux computed %.8e exact %.8e | error "
              "%.4e (%.4f%%) | worst face %.4f%% | sum explicit %.4e\n",
              static_cast<std::size_t>(nx), static_cast<std::size_t>(ny),
              static_cast<std::size_t>(faces), totalComputed, totalExact,
              totalComputed - totalExact,
              100.0 * (totalComputed - totalExact) / std::abs(totalExact), 100.0 * worstRelative,
              totalExplicit);
}

}  // namespace

int main() {
  std::printf("# INV-001 wall diffusive flux with the exact field, production formula\n");
  study(64, 8);
  study(96, 12);
  study(144, 18);
  return 0;
}
