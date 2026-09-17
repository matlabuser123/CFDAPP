// P12-GRAD-001: is the residual translated-vs-original boundary-gradient difference (GR1, quadratic
// field) an irreducible round-off floor of the shoelace geometry, or a remaining defect? Evidence only.
//
// For each resolution: the same Cartesian mesh translated by
//   - a NON-representable offset (0.005, 0.0025): the geometry differs by round-off;
//   - a DYADIC offset (1/128, 1/256): every coordinate is exact, so the geometry is exact.
// If the residual is round-off, the dyadic case is ~0 and the non-dyadic case grows steeply with
// resolution. Also prints the max misalignment and the predicate's tolerance, to show every boundary
// face is classified aligned in both runs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

std::vector<Vector3> vertices(Index n, const Vector3& offset) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n)) + offset.x,
                          (static_cast<Real>(j) / static_cast<Real>(n)) + offset.y, 0.0});
    }
  }
  return v;
}

Real quadratic(const Vector3& x) { return (0.5 * x.x * x.x) + (0.25 * x.y * x.y); }

boundary::BoundaryConditionSet zeroValue(const mesh::Mesh& m) {
  boundary::BoundaryConditionSet set;
  for (const auto& patch : m.boundaryPatches()) {
    set.set(m, patch.name(), std::make_unique<boundary::FixedValue>(0.0));
  }
  return set;
}

fields::ScalarField sample(const mesh::Mesh& m, const Vector3& shift) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& cell : m.cells()) phi[cell.id()] = quadratic(cell.centroid() - shift);
  return phi;
}

struct Result {
  Real boundary{0.0};
  Real interior{0.0};
  Real maxSine{0.0};
  Real minTolerance{1e300};
  Index notAligned{0};
};

Result compare(Index n, const Vector3& offset) {
  const mesh::Mesh base = mesh::MeshGeometry::createStructuredQuad2D(n, n, vertices(n, Vector3{}));
  const mesh::Mesh moved = mesh::MeshGeometry::createStructuredQuad2D(n, n, vertices(n, offset));
  const auto gA = discretization::gradient(base, sample(base, Vector3{}), zeroValue(base),
                                           discretization::GradientScheme::GreenGauss);
  const auto gB = discretization::gradient(moved, sample(moved, offset), zeroValue(moved),
                                           discretization::GradientScheme::GreenGauss);
  Result r;
  for (const auto& cell : base.cells()) {
    bool interior = true;
    for (const Index f : cell.faceIds()) {
      if (base.face(f).isBoundary()) interior = false;
    }
    const Real d = magnitude(gA[cell.id()] - gB[cell.id()]);  // |grad| scale is 1 for this field
    if (interior) {
      r.interior = std::max(r.interior, d);
    } else {
      r.boundary = std::max(r.boundary, d);
    }
  }
  for (const auto& face : moved.faces()) {
    if (!face.isBoundary()) continue;
    const auto a = mesh::MeshGeometry::boundaryFaceAlignment(moved, face);
    r.maxSine = std::max(r.maxSine, a.sine);
    r.minTolerance = std::min(r.minTolerance, a.tolerance);
    if (!a.aligned) ++r.notAligned;
  }
  return r;
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-001: round-off floor of the translated-vs-original boundary gradient "
              "(quadratic field, post-fix)\n");
  for (const Index n : {16u, 32u, 64u, 128u}) {
    for (const auto& [name, offset] : std::vector<std::pair<const char*, Vector3>>{
             {"non-dyadic (0.005, 0.0025)", Vector3{0.005, 0.0025, 0.0}},
             {"dyadic (1/128, 1/256)", Vector3{1.0 / 128.0, 1.0 / 256.0, 0.0}}}) {
      const Result r = compare(n, offset);
      std::printf("%4zu^2 %-28s boundary %.3e interior %.3e | max sine %.3e min tolerance %.3e "
                  "not-aligned faces %zu\n",
                  static_cast<std::size_t>(n), name, r.boundary, r.interior, r.maxSine,
                  r.minTolerance, r.notAligned);
    }
  }
  return 0;
}
