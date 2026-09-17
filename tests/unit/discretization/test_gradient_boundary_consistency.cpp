// P12-GRAD-002: regression tests for the continuous (branch-free) Green-Gauss boundary treatment
// (results/p12-grad-002/formulation.md). Every test is a numerical property derived in the phase's
// gates -- acceptance_gate.md (C2, C3(a), C5, C13), acceptance_gate_A1.md (C1-A1, C4a) and
// acceptance_gate_A2.md (the planar / warped-face split of C2) -- not a recorded value. Their
// outcome on the pre-GRAD-002 operator was pre-registered and dry-run
// (results/p12-grad-002/a2/dryrun.md):
//   - constant / linear exactness, aligned-Cartesian quadratic exactness and the degenerate cells
//     hold for both operators (they guard what GRAD-002 had to keep);
//   - the translation family's quadratic rows and the continuity sweep FAIL on the pre-GRAD-002
//     operator: its exact `cross(d, S_f) == 0` predicate made a translated Cartesian mesh change
//     discretization (P12-MESH-007 G6.3).
//
// Floating-point envelope (A1 section 1): a gradient discrepancy is bounded by
//   E = eps * 8 * max|phi| * max_P(sum_f |S_f| / V_P) + 100 * C_g * max|grad phi|,
// C_g being the mesh's measured geometry inconsistency in units of h.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Cell;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kEpsilon = std::numeric_limits<Real>::epsilon();
constexpr Real kFaceRounding = 8.0;    // A1 K_face
constexpr Real kGeometryGain = 100.0;  // A1 K_geom

std::vector<Vector3> gridVertices(Index n, Real length, const Vector3& offset) {
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      vertices.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n) * length) + offset.x,
                                 (static_cast<Real>(j) / static_cast<Real>(n) * length) + offset.y,
                                 0.0});
    }
  }
  return vertices;
}

Mesh quad(Index n, Real length, const Vector3& offset) {
  return MeshGeometry::createStructuredQuad2D(n, n, gridVertices(n, length, offset));
}

// The distorted mesh Q16 of the GRAD-002 gates (vertex amplitude 0.03, boundary kept straight).
Mesh q16(const Vector3& offset) {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      vertices.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)) + offset.x,
                                 y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)) + offset.y,
                                 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(16, 16, vertices);
}

// C5's family (results/p12-grad-002/tools/diagnostics.cpp): a Cartesian 16^2 unit square whose
// STRICTLY interior vertices move tangentially by +/- amplitude * h, so every boundary face keeps
// its exact axis-aligned normal while the boundary cells' centroids leave their face normals.
Mesh sheared(Real amplitude) {
  const Index n = 16;
  std::vector<Vector3> vertices = gridVertices(n, 1.0, Vector3{});
  for (Index j = 1; j < n; ++j) {
    for (Index i = 1; i < n; ++i) {
      vertices[(j * (n + 1)) + i].x +=
          amplitude * (1.0 / static_cast<Real>(n)) * ((j % 2 == 0) ? 1.0 : -1.0);
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

// A 3D mesh whose faces are all PLANAR but whose cells are graded, skewed and non-orthogonal at
// every boundary (A2's planar family): x' = x + a sin(pi x) + a sin(2 pi y + 0.3) sin(pi x),
// y' = y + a sin(pi y), z' = z + 0.3 y', a = 0.03. Each face's corners are a sum of a function of
// one index and a function of another, so every face is a parallelogram-like planar quad, and
// every patch is a plane.
class PlanarSkewMotion final : public cfd::mesh::PrescribedMotion {
 public:
  [[nodiscard]] Vector3 position(const Vector3& x, Real elapsed) const override {
    const Real pi = cfd::constants::pi;
    const Real y = x.y + (0.03 * std::sin(pi * x.y));
    const Vector3 target{x.x + (0.03 * std::sin(pi * x.x)) +
                             (0.03 * std::sin((2.0 * pi * x.y) + 0.3) * std::sin(pi * x.x)),
                         y, x.z + (0.3 * y)};
    return x + ((target - x) * elapsed);
  }
  [[nodiscard]] std::string description() const override { return "planar skew"; }
};

Mesh planarSkew3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(mesh, std::make_shared<PlanarSkewMotion>());
  (void)motion.advance(1.0);
  return mesh;
}

// A1/A2's warped family: the box deformed sinusoidally (interior faces become bilinear, the box
// faces stay planar).
Mesh warped3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(
      mesh, std::make_shared<cfd::mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                          Vector3{0.05, 0.025, -0.0375},
                                                          cfd::constants::twoPi / 0.4));
  (void)motion.advance(0.1);
  return mesh;
}

struct AnalyticField {
  Real (*value)(const Vector3& x, const Vector3& origin);
  Vector3 (*gradient)(const Vector3& x, const Vector3& origin);
  bool axial;  // exact conditions: FixedValue on constant-x planes, zero gradient elsewhere
};

Real constantValue(const Vector3& /*x*/, const Vector3& /*o*/) { return 2.5; }
Vector3 constantGradient(const Vector3& /*x*/, const Vector3& /*o*/) { return Vector3{}; }
Real linearValue(const Vector3& x, const Vector3& o) {
  return 0.7 + (1.3 * (x.x - o.x)) - (0.9 * (x.y - o.y)) + (0.4 * (x.z - o.z));
}
Vector3 linearGradient(const Vector3& /*x*/, const Vector3& /*o*/) {
  return Vector3{1.3, -0.9, 0.4};
}
Real quadraticValue(const Vector3& x, const Vector3& o) { return 0.5 * (x.x - o.x) * (x.x - o.x); }
Vector3 quadraticGradient(const Vector3& x, const Vector3& o) { return Vector3{x.x - o.x, 0, 0}; }

const AnalyticField kConstant{constantValue, constantGradient, false};
const AnalyticField kLinear{linearValue, linearGradient, false};
const AnalyticField kQuadratic{quadraticValue, quadraticGradient, true};

Vector3 inPlane(const Mesh& mesh, const Vector3& g) {
  return mesh.dimension() == 3 ? g : Vector3{g.x, g.y, 0.0};
}

// Exact per-patch conditions for the field; `exact` is false if some face is not exactly
// represented (a patch that is not a plane of the kind the condition needs).
BoundaryConditionSet exactConditions(const Mesh& mesh, const AnalyticField& field,
                                     const Vector3& origin, bool& exact) {
  BoundaryConditionSet set;
  exact = true;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Face& first = mesh.face(patch.faceIds().front());
    const Vector3 n0 = first.areaVector() * (1.0 / first.area());
    bool flat = true;
    for (const Index id : patch.faceIds()) {
      const Face& face = mesh.face(id);
      if (magnitude((face.areaVector() * (1.0 / face.area())) - n0) > 1e-12) flat = false;
    }
    if (!field.axial) {
      if (!flat) exact = false;
      set.set(mesh, patch.name(),
              std::make_unique<FixedGradient>(
                  dot(inPlane(mesh, field.gradient(first.centroid(), origin)), n0)));
      continue;
    }
    const bool constantX = flat && std::abs(std::abs(n0.x) - 1.0) < 1e-12;
    if (constantX) {
      const Real x = first.centroid().x;
      for (const Index id : patch.faceIds()) {
        if (std::abs(mesh.face(id).centroid().x - x) > 1e-12 * std::max(1.0, std::abs(x))) {
          exact = false;
        }
      }
      set.set(mesh, patch.name(),
              std::make_unique<FixedValue>(field.value(Vector3{x, 0, 0}, origin)));
    } else {
      if (!(flat && std::abs(n0.x) <= 1e-12)) exact = false;
      set.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
    }
  }
  return set;
}

ScalarField sample(const Mesh& mesh, const AnalyticField& field, const Vector3& origin) {
  ScalarField phi(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) phi[cell.id()] = field.value(cell.centroid(), origin);
  return phi;
}

VectorField gradientOf(const Mesh& mesh, const AnalyticField& field, const Vector3& origin,
                       Index sweeps = cfd::discretization::kGreenGaussSkewCorrectionSweeps) {
  bool exact = false;
  const BoundaryConditionSet conditions = exactConditions(mesh, field, origin, exact);
  EXPECT_TRUE(exact) << "the test's boundary conditions do not represent the field exactly";
  return cfd::discretization::greenGaussGradient(mesh, sample(mesh, field, origin), conditions,
                                                 sweeps);
}

Real maxError(const Mesh& mesh, const VectorField& g, const AnalyticField& field,
              const Vector3& origin) {
  Real worst = 0.0;
  for (const auto& cell : mesh.cells()) {
    worst = std::max(
        worst, magnitude(g[cell.id()] - inPlane(mesh, field.gradient(cell.centroid(), origin))));
  }
  return worst;
}

// A1's envelope for `mesh`, the field, and a measured geometry inconsistency C_g.
Real envelope(const Mesh& mesh, const AnalyticField& field, const Vector3& origin, Real cg) {
  Real phi = 0.0;
  Real grad = 0.0;
  Real conditioning = 0.0;
  for (const auto& cell : mesh.cells()) {
    phi = std::max(phi, std::abs(field.value(cell.centroid(), origin)));
    grad = std::max(grad, magnitude(inPlane(mesh, field.gradient(cell.centroid(), origin))));
    Real area = 0.0;
    for (const Index id : cell.faceIds()) area += mesh.face(id).area();
    conditioning = std::max(conditioning, area / cell.volume());
  }
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) phi = std::max(phi, std::abs(field.value(face.centroid(), origin)));
  }
  return (kEpsilon * kFaceRounding * phi * conditioning) + (kGeometryGain * cg * grad);
}

// C_g of an orthogonal quad mesh: its centroids against the exact Cartesian ones, / h.
Real cartesianCentroidError(const Mesh& mesh, Index n, Real length, const Vector3& offset) {
  const Real h = length / static_cast<Real>(n);
  Real worst = 0.0;
  for (Index j = 0; j < n; ++j) {
    for (Index i = 0; i < n; ++i) {
      const Vector3 exact{((static_cast<Real>(i) + 0.5) * h) + offset.x,
                          ((static_cast<Real>(j) + 0.5) * h) + offset.y, 0.0};
      worst = std::max(worst, magnitude(mesh.cell((j * n) + i).centroid() - exact) / h);
    }
  }
  return worst;
}

// ---- A2: independent bilinear-face quadrature and the lagged-sweep feedback bound -------------

Real quadratureLinearValue(const Vector3& x) { return linearValue(x, Vector3{}); }

// integral(phi n dS) over the bilinear face p0..p3 (MeshGeometry::StructuredTopology ordering),
// 4x4 Gauss: exact for a linear phi (degree <= 2 per variable).
Vector3 faceIntegral(const std::array<Vector3, 4>& p) {
  const Real x1 = std::sqrt((3.0 / 7.0) - ((2.0 / 7.0) * std::sqrt(6.0 / 5.0)));
  const Real x2 = std::sqrt((3.0 / 7.0) + ((2.0 / 7.0) * std::sqrt(6.0 / 5.0)));
  const Real w1 = (18.0 + std::sqrt(30.0)) / 36.0;
  const Real w2 = (18.0 - std::sqrt(30.0)) / 36.0;
  const std::array<Real, 4> node{0.5 - (0.5 * x2), 0.5 - (0.5 * x1), 0.5 + (0.5 * x1),
                                 0.5 + (0.5 * x2)};
  const std::array<Real, 4> weight{0.5 * w2, 0.5 * w1, 0.5 * w1, 0.5 * w2};
  Vector3 sum{};
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      const Real u = node[i];
      const Real v = node[j];
      const Vector3 x = (p[0] * ((1.0 - u) * (1.0 - v))) + (p[1] * (u * (1.0 - v))) +
                        (p[2] * (u * v)) + (p[3] * ((1.0 - u) * v));
      const Vector3 du = ((p[1] - p[0]) * (1.0 - v)) + ((p[2] - p[3]) * v);
      const Vector3 dv = ((p[3] - p[0]) * (1.0 - u)) + ((p[2] - p[1]) * u);
      sum = sum + (cross(du, dv) * (weight[i] * weight[j] * quadratureLinearValue(x)));
    }
  }
  return sum;
}

// PG_P = (1/V_P) sum_f s (phi(x_f) S_f - integral(phi n dS)): the error plain Green-Gauss makes
// with EXACT face values (a pure property of the face representation; 0 on planar faces).
std::vector<Vector3> warpDefect(const Mesh& mesh) {
  const auto topology = MeshGeometry::structuredTopology(mesh);
  std::vector<Vector3> faceTerm(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    const auto& fv = topology.faces[face.id()];
    faceTerm[face.id()] =
        (face.areaVector() * quadratureLinearValue(face.centroid())) -
        faceIntegral({topology.vertices[fv.vertex[0]], topology.vertices[fv.vertex[1]],
                      topology.vertices[fv.vertex[2]], topology.vertices[fv.vertex[3]]});
  }
  std::vector<Vector3> defect(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    Vector3 sum{};
    for (const Index id : cell.faceIds()) {
      sum = sum + (faceTerm[id] * ((mesh.face(id).owner() == cell.id()) ? 1.0 : -1.0));
    }
    defect[cell.id()] = sum * (1.0 / cell.volume());
  }
  return defect;
}

// For a linear field the lagged sweeps obey e_{K+1} = PG + A e_K (K >= 1), A collecting every
// term that uses the previous sweep's gradient (skew-corrected interior faces, oblique Neumann
// transfer, the GRAD-002 opposite-face correction). By the triangle inequality, per cell,
// |e_{K+1,P} - PG_P| <= this bound evaluated on e_K (results/p12-grad-002/a2/tools/a2_3d.cpp).
std::vector<Real> feedbackBound(const Mesh& mesh, const BoundaryConditionSet& conditions,
                                const std::vector<Vector3>& e) {
  std::vector<Real> bound(mesh.numberOfCells(), 0.0);
  for (const auto& cell : mesh.cells()) {
    const Index p = cell.id();
    Real sum = 0.0;
    for (const Index id : cell.faceIds()) {
      const Face& face = mesh.face(id);
      if (!face.isBoundary()) {
        const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, face);
        if (crossing.has_value() && crossing->skewVector != Vector3{}) {
          const Index q = (face.owner() == p) ? *face.neighbor() : face.owner();
          sum += face.area() * magnitude(crossing->skewVector) *
                 std::max(magnitude(e[p]), magnitude(e[q]));
        }
        continue;
      }
      const Vector3 n = face.areaVector() * (1.0 / face.area());
      Vector3 tangential{};
      if (!cfd::discretization::prescribesBoundaryValue(
              cfd::boundary::boundaryConditionForFace(mesh, id, conditions).type())) {
        const Vector3 d = face.centroid() - cell.centroid();
        tangential = d - (n * dot(d, n));
        sum += face.area() * magnitude(tangential) * magnitude(e[p]);
      }
      const auto opposite = MeshGeometry::oppositeInteriorFace(mesh, cell, face);
      if (!opposite.has_value()) continue;
      const Face& oppositeFace = mesh.face(*opposite);
      const Index far =
          (oppositeFace.owner() == p) ? *oppositeFace.neighbor() : oppositeFace.owner();
      const Vector3 along = mesh.cell(far).centroid() - cell.centroid();
      const Real length = magnitude(along);
      const Vector3 direction = along * (1.0 / length);
      const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, oppositeFace);
      const Real dn = dot(direction, n);
      if (!crossing.has_value() || !(std::abs(dn) > 0.0)) continue;
      const Real t = dot(cell.centroid() - face.centroid(), n) / dn;
      if (!(t > 0.0)) continue;
      const Vector3 onLine = cell.centroid() - (direction * t);
      const Real w = crossing->t;
      sum += oppositeFace.area() * (w * (1.0 - w) * length * length / (t * (t + length))) *
             magnitude(tangential + (onLine - face.centroid())) * magnitude(e[p]);
    }
    bound[p] = sum / cell.volume();
  }
  return bound;
}

// Two cells, [0,1]x[0,1] and [1,2]x[0,1]; cell 0's left side is split into TWO boundary faces,
// both anti-parallel to the shared face f0 -- two boundary faces claiming one opposite face.
// `reversed` lists cell 0's faces in the opposite order.
Mesh twoClaimMesh(bool reversed) {
  std::vector<Face> faces;
  faces.emplace_back(0, 0, Index{1}, Vector3{1.0, 0.5, 0}, Vector3{1.0, 0, 0});
  faces.emplace_back(1, 0, std::nullopt, Vector3{0.0, 0.25, 0}, Vector3{-0.5, 0, 0});
  faces.emplace_back(2, 0, std::nullopt, Vector3{0.0, 0.75, 0}, Vector3{-0.5, 0, 0});
  faces.emplace_back(3, 0, std::nullopt, Vector3{0.5, 0.0, 0}, Vector3{0, -1.0, 0});
  faces.emplace_back(4, 0, std::nullopt, Vector3{0.5, 1.0, 0}, Vector3{0, 1.0, 0});
  faces.emplace_back(5, 1, std::nullopt, Vector3{2.0, 0.5, 0}, Vector3{1.0, 0, 0});
  faces.emplace_back(6, 1, std::nullopt, Vector3{1.5, 0.0, 0}, Vector3{0, -1.0, 0});
  faces.emplace_back(7, 1, std::nullopt, Vector3{1.5, 1.0, 0}, Vector3{0, 1.0, 0});
  std::vector<Cell> cells;
  cells.emplace_back(0, Vector3{0.5, 0.5, 0}, 1.0);
  cells.emplace_back(1, Vector3{1.5, 0.5, 0}, 1.0);
  std::vector<Index> order{0, 1, 2, 3, 4};
  if (reversed) std::reverse(order.begin(), order.end());
  for (const Index id : order) cells[0].addFace(id);
  for (const Index id : {0u, 5u, 6u, 7u}) cells[1].addFace(id);
  std::vector<cfd::mesh::BoundaryPatch> patches;
  patches.emplace_back("left", std::vector<Index>{1, 2});
  patches.emplace_back("right", std::vector<Index>{5});
  patches.emplace_back("bottom", std::vector<Index>{3, 6});
  patches.emplace_back("top", std::vector<Index>{4, 7});
  return Mesh(std::move(cells), std::move(faces), std::move(patches));
}

}  // namespace

// C1-A1 / C2: a constant field has zero gradient to within the floating-point envelope, on
// aligned, translated, rescaled, graded, distorted and deformed meshes.
TEST(GradientBoundaryConsistency, ConstantFieldGradientIsZeroWithinTheFloatingPointEnvelope) {
  const Vector3 none{};
  const std::vector<std::pair<std::string, Mesh>> meshes = [&] {
    std::vector<std::pair<std::string, Mesh>> list;
    list.emplace_back("cartesian 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
    list.emplace_back("quad 16", quad(16, 1.0, none));
    list.emplace_back("quad 16 dyadic", quad(16, 1.0, Vector3{1.0 / 128.0, 1.0 / 256.0, 0}));
    list.emplace_back("quad 64 translated", quad(64, 1.0, Vector3{0.005, 0.0025, 0}));
    list.emplace_back("quad 16 L=1e-3", quad(16, 1e-3, Vector3{5e-6, 2.5e-6, 0}));
    list.emplace_back("quad 16 L=1e3", quad(16, 1e3, Vector3{5.0, 2.5, 0}));
    list.emplace_back(
        "graded 16",
        MeshGeometry::createGraded2D(
            16, 16, 1.0, 1.0, cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.2},
            cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.2}));
    list.emplace_back("Q16", q16(none));
    list.emplace_back("cartesian3d 8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
    list.emplace_back("planar skew 3d 8", planarSkew3D(8));
    list.emplace_back("warped 3d 8", warped3D(8));
    return list;
  }();
  for (const auto& [name, mesh] : meshes) {
    const VectorField g = gradientOf(mesh, kConstant, none);
    EXPECT_LE(maxError(mesh, g, kConstant, none), envelope(mesh, kConstant, none, 0.0)) << name;
  }
}

// C2 (aligned and translated Cartesian: 1e-11 relative; Q16 at the production sweep count: 1e-9)
// and C1-A1 (rescaled meshes, whose absolute floor is eps |phi| / h: the envelope with the
// Cartesian centroid error as C_g).
TEST(GradientBoundaryConsistency, LinearFieldIsReproducedOnOrthogonalAndDistortedMeshes) {
  const Vector3 none{};
  const Real reference = magnitude(Vector3{1.3, -0.9, 0.0});
  for (const auto& [name, mesh] : std::vector<std::pair<std::string, Mesh>>{
           {"cartesian 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0)},
           {"quad 16", quad(16, 1.0, none)},
           {"quad 16 dyadic", quad(16, 1.0, Vector3{1.0 / 128.0, 1.0 / 256.0, 0})},
           {"quad 64 translated", quad(64, 1.0, Vector3{0.005, 0.0025, 0})},
           {"graded 16",
            MeshGeometry::createGraded2D(
                16, 16, 1.0, 1.0, cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.2},
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.2})}}) {
    EXPECT_LE(maxError(mesh, gradientOf(mesh, kLinear, none), kLinear, none) / reference, 1e-11)
        << name;
  }
  for (const auto& [length, offset] : std::vector<std::pair<Real, Vector3>>{
           {1e-3, Vector3{5e-6, 2.5e-6, 0}}, {1e3, Vector3{5.0, 2.5, 0}}}) {
    const Mesh mesh = quad(16, length, offset);
    const Real cg = cartesianCentroidError(mesh, 16, length, offset);
    EXPECT_LE(maxError(mesh, gradientOf(mesh, kLinear, none), kLinear, none),
              envelope(mesh, kLinear, none, cg))
        << "L = " << length;
  }
  const Mesh distorted = q16(none);
  EXPECT_LE(maxError(distorted, gradientOf(distorted, kLinear, none), kLinear, none) / reference,
            1e-9);
  const Mesh cube = MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
  EXPECT_LE(maxError(cube, gradientOf(cube, kLinear, none), kLinear, none) /
                magnitude(Vector3{1.3, -0.9, 0.4}),
            1e-11);
}

// A2, planar faces: on a skewed, non-orthogonal 3D mesh whose faces are all planar, plain
// Green-Gauss with exact face values is exact, so the converged lagged iteration reproduces a
// linear field to the floating-point envelope.
TEST(GradientBoundaryConsistency, LinearFieldFixedPointIsExactOnPlanarFaced3DSkewedMesh) {
  const Vector3 none{};
  const Mesh mesh = planarSkew3D(8);
  const VectorField converged = gradientOf(mesh, kLinear, none, 64);
  EXPECT_LE(maxError(mesh, converged, kLinear, none), envelope(mesh, kLinear, none, 0.0));
}

// A2, warped faces: the linear-field error is the single-point face-quadrature defect PG of the
// warped (bilinear) faces, computed independently by exact Gauss quadrature, up to the
// lagged-sweep feedback -- checked cell by cell at every sweep 1..8. PG itself is far above
// round-off, so the check is not vacuous.
TEST(GradientBoundaryConsistency, LinearFieldErrorOnWarpedFacesIsTheFaceQuadratureDefect) {
  const Vector3 none{};
  const Mesh mesh = warped3D(8);
  bool exact = false;
  const BoundaryConditionSet conditions = exactConditions(mesh, kLinear, none, exact);
  ASSERT_TRUE(exact);
  const ScalarField phi = sample(mesh, kLinear, none);
  const std::vector<Vector3> defect = warpDefect(mesh);
  const Real floor = envelope(mesh, kLinear, none, 0.0);
  Real largestDefect = 0.0;
  for (const auto& d : defect) largestDefect = std::max(largestDefect, magnitude(d));
  EXPECT_GT(largestDefect, 1e6 * floor);

  const Vector3 a = linearGradient(none, none);
  std::vector<Vector3> previous;
  for (Index sweeps = 0; sweeps <= 8; ++sweeps) {
    const VectorField g = cfd::discretization::greenGaussGradient(mesh, phi, conditions, sweeps);
    std::vector<Vector3> error(mesh.numberOfCells());
    for (Index i = 0; i < mesh.numberOfCells(); ++i) error[i] = g[i] - a;
    if (sweeps >= 1) {
      const std::vector<Real> bound = feedbackBound(mesh, conditions, previous);
      Index over = 0;
      for (Index i = 0; i < mesh.numberOfCells(); ++i) {
        if (magnitude(error[i] - defect[i]) > bound[i] + floor) ++over;
      }
      EXPECT_EQ(over, 0u) << "sweep " << sweeps;
    }
    previous = std::move(error);
  }
}

// C3(a): on aligned Cartesian meshes a quadratic field's gradient is exact in every cell,
// boundary-adjacent ones included -- the property the deleted paired treatment provided.
TEST(GradientBoundaryConsistency, QuadraticFieldIsExactOnAlignedCartesianMeshes) {
  const Vector3 none{};
  const Vector3 dyadic{1.0 / 128.0, 1.0 / 256.0, 0.0};
  for (const auto& [name, mesh, origin] : std::vector<std::tuple<std::string, Mesh, Vector3>>{
           {"cartesian 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), none},
           {"cartesian 64", MeshGeometry::createCartesian2D(64, 64, 1.0, 1.0), none},
           {"quad 16 dyadic", quad(16, 1.0, dyadic), dyadic},
           {"cartesian3d 8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0), none}}) {
    const VectorField g = gradientOf(mesh, kQuadratic, origin);
    EXPECT_LE(maxError(mesh, g, kQuadratic, origin), 1e-11) << name;
  }
}

// C4a: a mesh and its rigidly translated copy, inside the valid-geometry domain, give the same
// gradient to within the envelope for constant, linear and quadratic fields; an exactly
// representable (dyadic) offset gives bit-identical gradients.
TEST(GradientBoundaryConsistency, TranslatedMeshGivesTheSameGradient) {
  const Vector3 small{0.005, 0.0025, 0.0};
  const Vector3 dyadic{1.0 / 128.0, 1.0 / 256.0, 0.0};
  const std::array<const AnalyticField*, 3> fields{&kConstant, &kLinear, &kQuadratic};
  const std::array<const char*, 3> names{"constant", "linear", "quadratic"};
  for (const Index n : {16u, 32u, 64u}) {
    const Mesh a = quad(n, 1.0, Vector3{});
    const Mesh b = quad(n, 1.0, small);
    const Mesh d = quad(n, 1.0, dyadic);
    const Real h = 1.0 / static_cast<Real>(n);
    Real cg = 0.0;
    for (const auto& cell : a.cells()) {
      cg = std::max(cg, magnitude(b.cell(cell.id()).centroid() - small - cell.centroid()) / h);
    }
    ASSERT_LE(cg, 1e-6) << "outside the valid-geometry domain (A1 C4a)";
    for (std::size_t f = 0; f < fields.size(); ++f) {
      const VectorField ga = gradientOf(a, *fields[f], Vector3{});
      const VectorField gb = gradientOf(b, *fields[f], small);
      const VectorField gd = gradientOf(d, *fields[f], dyadic);
      Real delta = 0.0;
      for (Index i = 0; i < a.numberOfCells(); ++i) {
        delta = std::max(delta, magnitude(gb[i] - ga[i]));
        ASSERT_EQ(gd[i].x, ga[i].x) << names[f] << " dyadic, cell " << i;
        ASSERT_EQ(gd[i].y, ga[i].y) << names[f] << " dyadic, cell " << i;
      }
      EXPECT_LE(delta, envelope(b, *fields[f], small, cg)) << names[f] << ", n = " << n;
    }
  }
  const Mesh qa = q16(Vector3{});
  const Mesh qb = q16(small);
  Real cg = 0.0;
  for (const auto& cell : qa.cells()) {
    cg = std::max(cg, magnitude(qb.cell(cell.id()).centroid() - small - cell.centroid()) * 16.0);
  }
  for (std::size_t f = 0; f < fields.size(); ++f) {
    const VectorField ga = gradientOf(qa, *fields[f], Vector3{});
    const VectorField gb = gradientOf(qb, *fields[f], small);
    Real delta = 0.0;
    for (Index i = 0; i < qa.numberOfCells(); ++i)
      delta = std::max(delta, magnitude(gb[i] - ga[i]));
    EXPECT_LE(delta, envelope(qb, *fields[f], small, cg)) << names[f] << ", Q16";
  }
}

// C5, the decisive test: as the boundary misalignment m_f sweeps 0 -> 1e-2 with the same field,
// the gradient error changes continuously: |de| <= 10 dm + 1e-12 at every step. A formulation
// with a geometric branch jumps by O(h) where the branch switches.
TEST(GradientBoundaryConsistency, GradientErrorIsContinuousInTheBoundaryMisalignment) {
  const auto misalignment = [](const Mesh& mesh) {
    Real worst = 0.0;
    for (const auto& face : mesh.faces()) {
      if (!face.isBoundary()) continue;
      const Vector3 d = face.centroid() - mesh.cell(face.owner()).centroid();
      worst =
          std::max(worst, magnitude(cross(d, face.areaVector())) / (magnitude(d) * face.area()));
    }
    return worst;
  };
  Real previousError = 0.0;
  Real previousM = 0.0;
  bool first = true;
  for (const Real amplitude : {0.0, 5e-13, 5e-11, 5e-9, 5e-7, 5e-6, 5e-5, 5e-4, 5e-3}) {
    const Mesh mesh = sheared(amplitude);
    const VectorField g = gradientOf(mesh, kQuadratic, Vector3{});
    const Real error = maxError(mesh, g, kQuadratic, Vector3{});
    const Real m = misalignment(mesh);
    if (first) {
      EXPECT_LE(error, 1e-11) << "the unsheared member is an aligned Cartesian mesh (C3(a))";
      first = false;
    } else {
      EXPECT_LE(std::abs(error - previousError), (10.0 * std::abs(m - previousM)) + 1e-12)
          << "amplitude " << amplitude << ": m_f " << previousM << " -> " << m << ", error "
          << previousError << " -> " << error;
    }
    previousError = error;
    previousM = m;
  }
}

// Degenerate cell 1: a boundary face whose only "opposite" is another boundary face (a
// one-cell-thick domain, and a single cell) takes the topological fallback -- plain Green-Gauss
// for that face -- which is still exact for a linear field and translation invariant.
TEST(GradientBoundaryConsistency, CellWithoutAnOppositeInteriorFaceFallsBackAndStaysExact) {
  const Vector3 small{0.005, 0.0025, 0.0};
  const auto strip = [](Index nx, const Vector3& offset) {
    std::vector<Vector3> vertices;
    for (Index j = 0; j <= 1; ++j) {
      for (Index i = 0; i <= nx; ++i) {
        vertices.push_back(Vector3{(0.25 * static_cast<Real>(i)) + offset.x,
                                   (0.25 * static_cast<Real>(j)) + offset.y, 0.0});
      }
    }
    return MeshGeometry::createStructuredQuad2D(nx, 1, vertices);
  };
  const Real reference = magnitude(Vector3{1.3, -0.9, 0.0});
  for (const Index nx : {1u, 4u}) {
    const Mesh a = strip(nx, Vector3{});
    const Mesh b = strip(nx, small);
    const VectorField ga = gradientOf(a, kLinear, Vector3{});
    const VectorField gb = gradientOf(b, kLinear, small);
    for (Index i = 0; i < a.numberOfCells(); ++i) {
      ASSERT_TRUE(std::isfinite(ga[i].x) && std::isfinite(ga[i].y)) << "cell " << i;
    }
    EXPECT_LE(maxError(a, ga, kLinear, Vector3{}) / reference, 1e-11) << nx << "x1";
    Real delta = 0.0;
    for (Index i = 0; i < a.numberOfCells(); ++i) delta = std::max(delta, magnitude(gb[i] - ga[i]));
    EXPECT_LE(delta, envelope(b, kLinear, small, 0.0)) << nx << "x1 translated";
  }
}

// Degenerate cell 2: two boundary faces of one cell claiming the same opposite face resolve
// deterministically (most anti-parallel, then lowest boundary face id) -- independent of the
// order in which the cell lists its faces -- and the result is still exact for a linear field.
// The two orders sum the same terms in a different order, so they agree to round-off (the A1
// face-rounding floor), not bit for bit; a claim resolved by visiting order would instead pick
// the other face's value, an O(0.1) difference for the quadratic field here.
TEST(GradientBoundaryConsistency, TwoBoundaryFacesClaimingOneOppositeFaceResolveDeterministically) {
  const Mesh forward = twoClaimMesh(false);
  const Mesh backward = twoClaimMesh(true);
  const Vector3 gradient{1.3, -0.9, 0.0};
  const Real reference = magnitude(gradient);
  for (const auto* field : {&kLinear, &kQuadratic}) {
    // Linear: exact Neumann conditions. Quadratic: arbitrary but identical conditions -- only
    // the order independence is claimed for it.
    BoundaryConditionSet fc;
    BoundaryConditionSet bc;
    for (const auto* mesh : {&forward, &backward}) {
      auto& set = (mesh == &forward) ? fc : bc;
      set.set(*mesh, "left", std::make_unique<FixedGradient>(-gradient.x));
      set.set(*mesh, "right", std::make_unique<FixedGradient>(gradient.x));
      set.set(*mesh, "bottom", std::make_unique<FixedGradient>(-gradient.y));
      set.set(*mesh, "top", std::make_unique<FixedGradient>(gradient.y));
    }
    const VectorField gf = cfd::discretization::gradient(
        forward, sample(forward, *field, Vector3{}), fc, GradientScheme::GreenGauss);
    const VectorField gb = cfd::discretization::gradient(
        backward, sample(backward, *field, Vector3{}), bc, GradientScheme::GreenGauss);
    const Real floor = envelope(forward, *field, Vector3{}, 0.0);
    for (Index i = 0; i < 2; ++i) {
      ASSERT_TRUE(std::isfinite(gf[i].x) && std::isfinite(gf[i].y)) << "cell " << i;
      EXPECT_LE(magnitude(gf[i] - gb[i]), floor) << "cell " << i;
    }
    if (field == &kLinear) {
      for (Index i = 0; i < 2; ++i) {
        EXPECT_LE(magnitude(gf[i] - gradient) / reference, 1e-11) << "cell " << i;
      }
    }
  }
}
