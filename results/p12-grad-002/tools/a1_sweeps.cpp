// P12-GRAD-002 A1 diagnostic: why does the DISTORTED family's Linf order dip to 1.35 at 128^2
// while its L2 order holds 1.98? Hypothesis: the limit is P12-NUM-003's skewness-correction
// fixed-point truncation (kGreenGaussSkewCorrectionSweeps = 4), not the boundary reconstruction.
// Test: the same measurement through the public greenGaussGradient(..., sweeps) overload at 4, 8
// and 16 sweeps. If the error falls with sweeps, the sweep count is the limit.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

std::vector<Vector3> distorted(Index n) {
  std::vector<Vector3> v;
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

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 A1 diagnostic: distorted-family Linf vs skew-correction sweeps\n");
  for (const Index n : {16u, 32u, 64u, 128u}) {
    const mesh::Mesh m = mesh::MeshGeometry::createStructuredQuad2D(n, n, distorted(n));
    fields::ScalarField phi(m.numberOfCells());
    for (const auto& c : m.cells()) {
      phi[c.id()] = c.centroid().x * c.centroid().x * c.centroid().x;
    }
    boundary::BoundaryConditionSet bc;
    for (const auto& patch : m.boundaryPatches()) {
      const auto& first = m.face(patch.faceIds().front());
      const Vector3 nrm = first.areaVector() * (1.0 / first.area());
      if (std::abs(std::abs(nrm.x) - 1.0) < 1e-12) {
        const Real x = first.centroid().x;
        bc.set(m, patch.name(), std::make_unique<boundary::FixedValue>(x * x * x));
      } else {
        bc.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
      }
    }
    for (const Index sweeps : {4u, 8u, 16u}) {
      const auto g = discretization::greenGaussGradient(m, phi, bc, sweeps);
      Real linf = 0.0;
      Real sum = 0.0;
      for (const auto& c : m.cells()) {
        const Real e = magnitude(g[c.id()] - Vector3{3.0 * c.centroid().x * c.centroid().x, 0, 0});
        linf = std::max(linf, e);
        sum += e * e;
      }
      std::printf("SW  distorted n %4zu sweeps %2zu Linf %.3e L2 %.3e\n",
                  static_cast<std::size_t>(n), static_cast<std::size_t>(sweeps), linf,
                  std::sqrt(sum / static_cast<Real>(m.numberOfCells())));
    }
  }
  return 0;
}
