// P12-GRAD-001 gradient probe: the quantities of the frozen gradient-fix gate, measured through public
// APIs only, so the same program runs against BASE (pre-fix) and NEW (post-fix).
//
//   A  translation and scale invariance -- the Green-Gauss gradient of an analytic field on a Cartesian
//      mesh against the same mesh translated by a non-dyadic offset (or rescaled), cell by cell, with
//      the same field values and the same boundary conditions on both. Reported separately for interior
//      cells and boundary-adjacent cells, because the paired boundary treatment is what changes.
//   D  analytical accuracy -- max/L2 error of the computed gradient against the analytical gradient.
//      For the constant and linear fields on meshes whose patches are flat (every Cartesian variant,
//      2D and 3D), each patch carries the EXACT normal derivative as a FixedGradient condition, so the
//      boundary face values are exact and boundary cells are verified too. For the quadratic field, and
//      for the distorted mesh (whose patch normals vary along the patch, which a patch-wise condition
//      cannot represent), only interior cells are compared; that limitation is reported, not hidden.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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

struct Analytic {
  std::string name;
  std::function<Real(const Vector3&)> value;
  std::function<Vector3(const Vector3&)> gradient;
  bool flatPatchExact{true};  // is the normal derivative constant on a flat patch?
};

std::vector<Analytic> fields(bool threeD) {
  std::vector<Analytic> list;
  list.push_back({"constant", [](const Vector3&) { return 2.5; }, [](const Vector3&) { return Vector3{}; }, true});
  if (threeD) {
    list.push_back({"linear",
                    [](const Vector3& x) { return 0.7 + (1.3 * x.x) - (0.9 * x.y) + (0.4 * x.z); },
                    [](const Vector3&) { return Vector3{1.3, -0.9, 0.4}; }, true});
    list.push_back({"quadratic",
                    [](const Vector3& x) { return (0.5 * x.x * x.x) + (0.25 * x.y * x.y) + (0.1 * x.z * x.z); },
                    [](const Vector3& x) { return Vector3{x.x, 0.5 * x.y, 0.2 * x.z}; }, false});
  } else {
    list.push_back({"linear", [](const Vector3& x) { return 0.7 + (1.3 * x.x) - (0.9 * x.y); },
                    [](const Vector3&) { return Vector3{1.3, -0.9, 0.0}; }, true});
    list.push_back({"quadratic", [](const Vector3& x) { return (0.5 * x.x * x.x) + (0.25 * x.y * x.y); },
                    [](const Vector3& x) { return Vector3{x.x, 0.5 * x.y, 0.0}; }, false});
  }
  return list;
}

bool isInterior(const mesh::Mesh& m, const mesh::Cell& cell) {
  for (const Index f : cell.faceIds()) {
    if (m.face(f).isBoundary()) return false;
  }
  return true;
}

// Outward unit normal of a patch, and whether the patch is flat (all its faces
// share one normal to round-off).
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
    } else if (magnitude(n - result.normal) > 1e-9) {
      result.flat = false;
    }
  }
  return result;
}

// Exact Neumann conditions for a field whose normal derivative is constant on
// every (flat) patch; falls back to FixedValue(0) where that is impossible.
boundary::BoundaryConditionSet exactNeumann(const mesh::Mesh& m, const Analytic& f, bool& exact) {
  boundary::BoundaryConditionSet set;
  exact = f.flatPatchExact;
  for (const auto& patch : m.boundaryPatches()) {
    const PatchNormal n = patchNormal(m, patch);
    if (!n.flat || !f.flatPatchExact) {
      exact = false;
      set.set(m, patch.name(), std::make_unique<boundary::FixedValue>(0.0));
      continue;
    }
    const Vector3 g = f.gradient(m.face(patch.faceIds().front()).centroid());
    set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(dot(g, n.normal)));
  }
  return set;
}

fields::ScalarField sample(const mesh::Mesh& m, const std::function<Real(const Vector3&)>& f) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& cell : m.cells()) phi[cell.id()] = f(cell.centroid());
  return phi;
}

Real gradientScale(const mesh::Mesh& m, const Analytic& f) {
  Real scale = 0.0;
  for (const auto& cell : m.cells()) scale = std::max(scale, magnitude(f.gradient(cell.centroid())));
  return scale > 0.0 ? scale : 1.0;
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

std::vector<Vector3> distortedVertices(Index n) {
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

// A: same field values and boundary conditions on both meshes (the field on the
// translated mesh is f(x - offset)), so the exact gradients agree cell by cell.
void reportInvariance(const char* label, const mesh::Mesh& base, const mesh::Mesh& moved,
                      const Vector3& offset, bool threeD) {
  for (const auto& f : fields(threeD)) {
    Analytic shifted = f;
    shifted.value = [f, offset](const Vector3& x) { return f.value(x - offset); };
    shifted.gradient = [f, offset](const Vector3& x) { return f.gradient(x - offset); };
    bool exactA = false;
    bool exactB = false;
    const auto bcA = exactNeumann(base, f, exactA);
    const auto bcB = exactNeumann(moved, shifted, exactB);
    const auto gA = discretization::gradient(base, sample(base, f.value), bcA,
                                             discretization::GradientScheme::GreenGauss);
    const auto gB = discretization::gradient(moved, sample(moved, shifted.value), bcB,
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
    std::printf("A %-42s %-10s interior %.3e boundary %.3e\n", label, f.name.c_str(), interior, boundary);
  }
}

// D: analytic accuracy; boundary cells included only when the boundary values
// are exact (flat patches and a field with a constant normal derivative).
void reportAnalytic(const char* label, const mesh::Mesh& m, bool threeD) {
  for (const auto& f : fields(threeD)) {
    bool exact = false;
    const auto bc = exactNeumann(m, f, exact);
    const auto g = discretization::gradient(m, sample(m, f.value), bc,
                                            discretization::GradientScheme::GreenGauss);
    const Real scale = gradientScale(m, f);
    Real linfAll = 0.0;
    Real linfInterior = 0.0;
    Real l2Interior = 0.0;
    Index interiorCells = 0;
    for (const auto& cell : m.cells()) {
      const Real e = magnitude(g[cell.id()] - f.gradient(cell.centroid())) / scale;
      linfAll = std::max(linfAll, e);
      if (isInterior(m, cell)) {
        linfInterior = std::max(linfInterior, e);
        l2Interior += e * e;
        ++interiorCells;
      }
    }
    std::printf("D %-42s %-10s interior Linf %.3e L2 %.3e | all cells Linf %.3e (%s)\n", label,
                f.name.c_str(), linfInterior,
                interiorCells > 0 ? std::sqrt(l2Interior / static_cast<Real>(interiorCells)) : 0.0, linfAll,
                exact ? "boundary values exact" : "boundary values NOT representable patch-wise");
  }
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-001 gradient probe\n");
  const Vector3 offset2D{0.005, 0.0025, 0.0};
  for (const Index n : {16u, 64u}) {
    const mesh::Mesh plain = mesh::MeshGeometry::createStructuredQuad2D(n, n, cartesianVertices(n, 1.0, Vector3{}));
    const mesh::Mesh moved = mesh::MeshGeometry::createStructuredQuad2D(n, n, cartesianVertices(n, 1.0, offset2D));
    char label[96];
    std::snprintf(label, sizeof(label), "2D %zux%zu translated", static_cast<std::size_t>(n),
                  static_cast<std::size_t>(n));
    reportInvariance(label, plain, moved, offset2D, false);
  }
  for (const Real length : {1e-3, 1e3}) {
    const Vector3 offset{0.005 * length, 0.0025 * length, 0.0};
    const mesh::Mesh plain = mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesianVertices(16, length, Vector3{}));
    const mesh::Mesh moved = mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesianVertices(16, length, offset));
    char label[96];
    std::snprintf(label, sizeof(label), "2D 16x16 L = %g translated", length);
    reportInvariance(label, plain, moved, offset, false);
  }
  {
    const Vector3 offset{0.005, 0.0025, 0.001};
    const mesh::Mesh plain = mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
    mesh::Mesh moved = mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
    mesh::MeshMotion motion(moved, std::make_shared<mesh::AffineMotion>(mesh::AffineMotion::Matrix{}, Vector3{}, offset));
    (void)motion.advance(1.0);
    reportInvariance("3D 8^3 translated", plain, moved, offset, true);
  }
  reportAnalytic("2D 16x16 exact Cartesian", mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), false);
  reportAnalytic("2D 16x16 shoelace Cartesian",
                 mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesianVertices(16, 1.0, Vector3{})), false);
  reportAnalytic("2D 16x16 translated",
                 mesh::MeshGeometry::createStructuredQuad2D(16, 16, cartesianVertices(16, 1.0, offset2D)), false);
  reportAnalytic("2D 64x64 translated",
                 mesh::MeshGeometry::createStructuredQuad2D(64, 64, cartesianVertices(64, 1.0, offset2D)), false);
  reportAnalytic("2D 16x16 distorted",
                 mesh::MeshGeometry::createStructuredQuad2D(16, 16, distortedVertices(16)), false);
  reportAnalytic("3D 8^3 exact Cartesian", mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0), true);
  {
    mesh::Mesh moved = mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
    mesh::MeshMotion motion(moved, std::make_shared<mesh::AffineMotion>(mesh::AffineMotion::Matrix{}, Vector3{},
                                                                        Vector3{0.005, 0.0025, 0.001}));
    (void)motion.advance(1.0);
    reportAnalytic("3D 8^3 translated", moved, true);
  }
  return 0;
}
