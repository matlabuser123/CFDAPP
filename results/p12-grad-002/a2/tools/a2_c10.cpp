// P12-GRAD-002 A2 dry-run probe -- C10 (interior cells untouched) and C11(a) (aligned-Cartesian
// equivalence). Dumps, per mesh and field, every cell's Green-Gauss gradient after 3 and after 4
// (= production) sweeps as exact hex floats, plus what compare_c10.py needs to evaluate:
//   - the cell's layer: 1 = has a boundary face, n = n-1 faces away from layer 1;
//   - for every cell, its skewed interior faces (neighbor id, |S_f| |skew_f|), so the sweep
//     coupling bound  |dg4_P| <= (1/V_P) sum_f |S_f||skew_f| max(|dg3_P|, |dg3_N|) + floor
//     can be evaluated on the DIFFERENCE dg between two libraries (interior cells only: their
//     face values differ only through the skew correction, whose weight t is in [0, 1]);
//   - the A1 floor terms (max |phi|, max sum|S_f|/V).
// The same source builds against every library; the motion-based 3D meshes need MeshMotion.hpp
// and are skipped (reported) where it does not exist.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "DistortedMesh.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#if __has_include("cfd/mesh/MeshMotion.hpp")
#include "cfd/mesh/MeshMotion.hpp"
#define A2_HAS_MOTION 1
#else
#define A2_HAS_MOTION 0
#endif

using namespace cfd;

namespace {

std::vector<Vector3> gridVertices(Index n, Real length, const Vector3& o, Real shear, bool q16) {
  std::vector<Vector3> v;
  const Real pi = constants::pi;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real x = static_cast<Real>(i) / static_cast<Real>(n);
      const Real y = static_cast<Real>(j) / static_cast<Real>(n);
      Vector3 p{(x * length) + o.x, (y * length) + o.y, 0.0};
      if (q16) {
        p = Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                    y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0};
      }
      if (shear != 0.0 && i > 0 && i < n && j > 0 && j < n) {  // diagnostics.cpp's C5 family
        p.x += shear * (1.0 / static_cast<Real>(n)) * ((j % 2 == 0) ? 1.0 : -1.0);
      }
      v.push_back(p);
    }
  }
  return v;
}

// The W8 StructuredQuad mapping (test_structured_quad_production_case.cpp), 64x8 on [0,8]x[0,1].
std::vector<Vector3> productionQuad(Index nx, Index ny) {
  const Real pi = constants::pi;
  const Real length = 8.0;
  const Real height = 1.0;
  std::vector<Vector3> v;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = length * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = height * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (0.1 * std::sin(pi * xi / length) * std::sin(2.0 * pi * eta / height));
      Real y = eta + (0.05 * std::sin(2.0 * pi * xi / 1.0) * std::sin(pi * eta / height));
      if (i == 0) x = 0.0;
      if (i == nx) x = length;
      if (j == 0) y = 0.0;
      if (j == ny) y = height;
      v.push_back(Vector3{x, y, 0.0});
    }
  }
  return v;
}

struct Field {
  std::string name;
  Real (*value)(const Vector3&);
};
Real quadraticField(const Vector3& x) { return 0.5 * (x.x - 0.1) * (x.x - 0.1) + (0.3 * x.y); }
Real smoothField(const Vector3& x) {
  return std::sin((1.3 * x.x) + 0.2) * std::cos((0.9 * x.y) - 0.1) * std::cos(0.7 * x.z);
}
Real linearField(const Vector3& x) { return 0.7 + (1.3 * x.x) - (0.9 * x.y) + (0.4 * x.z); }

// Mixed conditions (the C10/C11 comparison needs identical data, not exact data): patches take
// FixedValue / FixedGradient alternately in patch order.
boundary::BoundaryConditionSet mixedConditions(const mesh::Mesh& m) {
  boundary::BoundaryConditionSet set;
  bool value = true;
  for (const auto& patch : m.boundaryPatches()) {
    if (value) {
      set.set(m, patch.name(), std::make_unique<boundary::FixedValue>(0.3));
    } else {
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(-0.2));
    }
    value = !value;
  }
  return set;
}

void dump(const char* label, const mesh::Mesh& m) {
  const Index n = m.numberOfCells();
  // layers
  std::vector<Index> layer(n, 0);
  std::deque<Index> queue;
  for (const auto& c : m.cells()) {
    for (const Index f : c.faceIds()) {
      if (m.face(f).isBoundary()) {
        layer[c.id()] = 1;
      }
    }
    if (layer[c.id()] == 1) queue.push_back(c.id());
  }
  while (!queue.empty()) {
    const Index p = queue.front();
    queue.pop_front();
    for (const Index f : m.cell(p).faceIds()) {
      const auto& face = m.face(f);
      if (face.isBoundary()) continue;
      const Index q = (face.owner() == p) ? *face.neighbor() : face.owner();
      if (layer[q] == 0) {
        layer[q] = layer[p] + 1;
        queue.push_back(q);
      }
    }
  }
  Index skewed = 0;
  Real conditioning = 0.0;
  for (const auto& f : m.faces()) {
    if (f.isBoundary()) continue;
    const auto c = mesh::MeshGeometry::ownerNeighborCrossing(m, f);
    if (c && c->skewVector != Vector3{}) ++skewed;
  }
  for (const auto& c : m.cells()) {
    Real area = 0.0;
    for (const Index f : c.faceIds()) area += m.face(f).area();
    conditioning = std::max(conditioning, area / c.volume());
  }
  const std::vector<Field> fields{
      {"quadratic", quadraticField}, {"smooth", smoothField}, {"linear", linearField}};
  for (const auto& field : fields) {
    fields::ScalarField phi(n);
    Real phiMax = 0.0;
    for (const auto& c : m.cells()) {
      phi[c.id()] = field.value(c.centroid());
      phiMax = std::max(phiMax, std::abs(phi[c.id()]));
    }
    const auto bc = mixedConditions(m);
    const auto g3 = discretization::greenGaussGradient(m, phi, bc, 3);
    const auto g4 = discretization::gradient(m, phi, bc, discretization::GradientScheme::GreenGauss);
    std::printf("MESH %s|%s %zu %d %zu %a %a\n", label, field.name.c_str(),
                static_cast<std::size_t>(n), m.dimension(), static_cast<std::size_t>(skewed),
                phiMax, conditioning);
    for (const auto& c : m.cells()) {
      const Index p = c.id();
      std::printf("C %zu %zu %a %a %a %a %a %a %a\n", static_cast<std::size_t>(p),
                  static_cast<std::size_t>(layer[p]), c.volume(), g3[p].x, g3[p].y, g3[p].z,
                  g4[p].x, g4[p].y, g4[p].z);
      for (const Index f : c.faceIds()) {
        const auto& face = m.face(f);
        if (face.isBoundary()) continue;
        const auto cr = mesh::MeshGeometry::ownerNeighborCrossing(m, face);
        if (!cr || cr->skewVector == Vector3{}) continue;
        const Index q = (face.owner() == p) ? *face.neighbor() : face.owner();
        std::printf("F %zu %zu %a %a\n", static_cast<std::size_t>(p), static_cast<std::size_t>(q),
                    face.area() * magnitude(cr->skewVector), cr->t);
      }
    }
  }
}

#if A2_HAS_MOTION
mesh::Mesh moved3D(Index n, const std::shared_ptr<const mesh::PrescribedMotion>& motion, Real t) {
  mesh::Mesh m = mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  mesh::MeshMotion mm(m, motion);
  (void)mm.advance(t);
  return m;
}
#endif

}  // namespace

int main() {
  const Vector3 none{};
  const Vector3 small{0.005, 0.0025, 0.0};
  const Vector3 dyadic{1.0 / 128.0, 1.0 / 256.0, 0.0};
  const Vector3 large{1234.5678, 987.6543, 0.0};
  using G = mesh::MeshGeometry;
  std::printf("# A2 C10/C11 dump; motion API %s\n", A2_HAS_MOTION ? "present" : "ABSENT (3D moved meshes skipped)");
  dump("aligned cart2d 16", G::createCartesian2D(16, 16, 1.0, 1.0));
  dump("aligned cart2d 64", G::createCartesian2D(64, 64, 1.0, 1.0));
  dump("aligned quad 16 plain", G::createStructuredQuad2D(16, 16, gridVertices(16, 1.0, none, 0.0, false)));
  dump("aligned quad 16 dyadic", G::createStructuredQuad2D(16, 16, gridVertices(16, 1.0, dyadic, 0.0, false)));
  dump("aligned quad 64 dyadic", G::createStructuredQuad2D(64, 64, gridVertices(64, 1.0, dyadic, 0.0, false)));
  dump("translated quad 16 small", G::createStructuredQuad2D(16, 16, gridVertices(16, 1.0, small, 0.0, false)));
  dump("translated quad 64 small", G::createStructuredQuad2D(64, 64, gridVertices(64, 1.0, small, 0.0, false)));
  dump("translated quad 16 LARGE", G::createStructuredQuad2D(16, 16, gridVertices(16, 1.0, large, 0.0, false)));
  dump("aligned graded 16 1.2",
       G::createGraded2D(16, 16, 1.0, 1.0, mesh::AxisGrading{mesh::GradingType::Geometric, 1.2},
                         mesh::AxisGrading{mesh::GradingType::Geometric, 1.2}));
  dump("aligned cart3d 8", G::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
  dump("skewed Q16", G::createStructuredQuad2D(16, 16, gridVertices(16, 1.0, none, 0.0, true)));
  dump("skewed C5 shear 5e-3", G::createStructuredQuad2D(16, 16, gridVertices(16, 1.0, none, 5e-3, false)));
  dump("skewed C5 shear 5e-9", G::createStructuredQuad2D(16, 16, gridVertices(16, 1.0, none, 5e-9, false)));
  dump("skewed NUM-003 20x20 0.45h", test::createDistortedQuad2D(20, 20, 1.0, 1.0, 0.0225));
  dump("skewed production quad 64x8", G::createStructuredQuad2D(64, 8, productionQuad(64, 8)));
#if A2_HAS_MOTION
  dump("translated cart3d 8 small",
       moved3D(8, std::make_shared<mesh::AffineMotion>(mesh::AffineMotion::Matrix{}, Vector3{},
                                                       Vector3{0.005, 0.0025, 0.00125}),
               1.0));
  dump("skewed warped3d 8",
       moved3D(8,
               std::make_shared<mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                        Vector3{0.05, 0.025, -0.0375},
                                                        constants::twoPi / 0.4),
               0.1));
#endif
  return 0;
}
