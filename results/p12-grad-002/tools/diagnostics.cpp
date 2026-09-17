// P12-GRAD-002 diagnostics. Runs identically against base (pre-MESH-007), grad001 and new, using
// only APIs present in every version.
//
//   F  floors: the quantities C1 and C4 bound, measured against their DIMENSIONAL round-off floors.
//      For phi = const the Green-Gauss gradient is phi * |sum_f S_f| / V, whose floor is ~eps*phi/h
//      -- a quantity that grows as the mesh shrinks, which C1's absolute 1e-13 bound ignored. Also
//      reports the mesh generator's own geometry error (cell volume and centroid against the exact
//      Cartesian values), which is what limits large-coordinate meshes regardless of any gradient
//      formulation.
//   S  continuity sweep (C5): a Cartesian 16^2 mesh sheared so the boundary misalignment m_f sweeps
//      0 -> 1e-2, with the same analytic field throughout. Reports the gradient error, the change
//      between consecutive members, and the Lipschitz quotient |de|/dm. A formulation with a
//      geometric branch jumps by O(1e-3) at its threshold; a continuous one cannot.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

constexpr Real kEpsilon = std::numeric_limits<Real>::epsilon();

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

// STRICTLY interior vertices displaced tangentially by +/- amplitude*h, so the
// domain boundary stays exactly the unit square (every boundary face keeps its
// exact axis-aligned normal and every patch stays flat, hence the prescribed
// boundary values stay exact) while the owner centroids of the boundary cells
// move off their face normals. m_f grows ~ amplitude.
//
// The first version of this family displaced i = 0 and i = n as well, which
// tilted the left/right boundary faces: above amplitude ~1.4e-6 the patches
// stopped being planes of constant x, axialConditions() then silently
// substituted a Neumann condition for the exact Dirichlet one, and every
// library -- including the unmodified pre-MESH-007 base -- showed a spurious
// O(1) jump there. That run is preserved in logs/04-06; `exact` below makes the
// instrument self-checking so such a substitution can never again be mistaken
// for a discontinuity of the discretization.
std::vector<Vector3> shearedVertices(Index n, Real amplitude) {
  std::vector<Vector3> v = cartesianVertices(n, 1.0, Vector3{});
  for (Index j = 1; j < n; ++j) {
    for (Index i = 1; i < n; ++i) {
      v[(j * (n + 1)) + i].x +=
          amplitude * (1.0 / static_cast<Real>(n)) * ((j % 2 == 0) ? 1.0 : -1.0);
    }
  }
  return v;
}

Real maxMisalignment(const mesh::Mesh& m) {
  Real worst = 0.0;
  for (const auto& face : m.faces()) {
    if (!face.isBoundary()) continue;
    const Vector3 d = face.centroid() - m.cell(face.owner()).centroid();
    const Vector3& sf = face.areaVector();
    const Real dm = magnitude(d);
    const Real sm = magnitude(sf);
    if (dm > 0.0 && sm > 0.0) worst = std::max(worst, magnitude(cross(d, sf)) / (dm * sm));
  }
  return worst;
}

bool isInterior(const mesh::Mesh& m, const mesh::Cell& cell) {
  for (const Index f : cell.faceIds()) {
    if (m.face(f).isBoundary()) return false;
  }
  return true;
}

// phi = (x - x0)^2 / 2 with exact per-patch boundary conditions: FixedValue on
// the constant-x patches, FixedGradient(0) on the others.
// `exact` reports whether EVERY face of EVERY patch is exactly represented by
// the condition assigned to it: a constant-x plane carrying the field's own
// value there, or a patch whose normal is exactly perpendicular to x carrying a
// zero normal derivative. A false here invalidates the measurement rather than
// producing a silent O(1) error.
boundary::BoundaryConditionSet axialConditions(const mesh::Mesh& m, Real x0, bool& exact) {
  boundary::BoundaryConditionSet set;
  exact = true;
  for (const auto& patch : m.boundaryPatches()) {
    const Vector3 first = m.face(patch.faceIds().front()).areaVector() *
                          (1.0 / m.face(patch.faceIds().front()).area());
    const bool constantX = std::abs(std::abs(first.x) - 1.0) < 1e-12;
    const Real x = m.face(patch.faceIds().front()).centroid().x;
    for (const Index faceId : patch.faceIds()) {
      const auto& face = m.face(faceId);
      const Vector3 n = face.areaVector() * (1.0 / face.area());
      if (constantX) {
        if (std::abs(std::abs(n.x) - 1.0) > 1e-12 ||
            std::abs(face.centroid().x - x) > 1e-12 * std::max(1.0, std::abs(x))) {
          exact = false;
        }
      } else if (std::abs(n.x) > 1e-12) {
        exact = false;
      }
    }
    if (constantX) {
      set.set(m, patch.name(), std::make_unique<boundary::FixedValue>(0.5 * (x - x0) * (x - x0)));
    } else {
      set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return set;
}

fields::ScalarField axialField(const mesh::Mesh& m, Real x0) {
  fields::ScalarField phi(m.numberOfCells());
  for (const auto& cell : m.cells()) {
    phi[cell.id()] = 0.5 * (cell.centroid().x - x0) * (cell.centroid().x - x0);
  }
  return phi;
}

void reportFloors(const char* label, const mesh::Mesh& m, Real length, Index n) {
  // Constant-field gradient: phi * |sum S_f| / V, and its dimensional floor.
  const Real phi = 2.5;
  fields::ScalarField constant(m.numberOfCells(), phi);
  boundary::BoundaryConditionSet set;
  for (const auto& patch : m.boundaryPatches()) {
    set.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
  }
  const auto g = discretization::gradient(m, constant, set,
                                          discretization::GradientScheme::GreenGauss);
  Real worstGradient = 0.0;
  Real worstClosure = 0.0;
  Real worstVolume = 0.0;
  Real worstCentroid = 0.0;
  const Real h = length / static_cast<Real>(n);
  const Real exactVolume = h * h;
  for (const auto& cell : m.cells()) {
    worstGradient = std::max(worstGradient, magnitude(g[cell.id()]));
    Vector3 closure{};
    for (const Index faceId : cell.faceIds()) {
      const auto& face = m.face(faceId);
      closure = closure + ((face.owner() == cell.id()) ? face.areaVector()
                                                       : (face.areaVector() * -1.0));
    }
    worstClosure = std::max(worstClosure, magnitude(closure) / cell.volume());
    worstVolume = std::max(worstVolume, std::abs(cell.volume() - exactVolume) / exactVolume);
  }
  // Centroid error against the exact Cartesian centroid, cell (i,j) -> ((i+0.5)h, (j+0.5)h).
  for (Index j = 0; j < n; ++j) {
    for (Index i = 0; i < n; ++i) {
      const auto& cell = m.cell((j * n) + i);
      const Vector3 exact{(static_cast<Real>(i) + 0.5) * h, (static_cast<Real>(j) + 0.5) * h, 0.0};
      const Vector3 actual = cell.centroid() - (m.cell(0).centroid() - Vector3{0.5 * h, 0.5 * h, 0});
      worstCentroid = std::max(worstCentroid, magnitude(actual - exact) / h);
    }
  }
  std::printf("F %-34s h %.3e | const-field |grad| %.3e (floor eps*phi/h %.3e) | closure/V %.3e | "
              "volume err %.3e | centroid err/h %.3e\n",
              label, h, worstGradient, kEpsilon * phi / h, worstClosure, worstVolume,
              worstCentroid);
}

void reportSweep(const char* label) {
  std::printf("# S %s: continuity sweep (axial quadratic field, exact boundary values)\n", label);
  const std::vector<Real> amplitudes{0.0, 5e-13, 5e-11, 5e-9, 5e-7, 5e-6, 5e-5, 5e-4, 5e-3};
  Real previousError = 0.0;
  Real previousBoundary = 0.0;
  Real previousM = 0.0;
  bool first = true;
  for (const Real amplitude : amplitudes) {
    const mesh::Mesh m =
        mesh::MeshGeometry::createStructuredQuad2D(16, 16, shearedVertices(16, amplitude));
    bool exact = false;
    const auto conditions = axialConditions(m, 0.0, exact);
    const auto g = discretization::gradient(m, axialField(m, 0.0), conditions,
                                            discretization::GradientScheme::GreenGauss);
    Real allCells = 0.0;
    Real boundaryCells = 0.0;
    for (const auto& cell : m.cells()) {
      const Real e = magnitude(g[cell.id()] - Vector3{cell.centroid().x, 0.0, 0.0});
      allCells = std::max(allCells, e);
      if (!isInterior(m, cell)) boundaryCells = std::max(boundaryCells, e);
    }
    const Real mf = maxMisalignment(m);
    if (first) {
      std::printf("S %-10s m_f %.3e | error %.3e boundary %.3e | (reference member) %s\n", label,
                  mf, allCells, boundaryCells, exact ? "" : "[BC NOT EXACT -- INVALID]");
      first = false;
    } else {
      const Real dm = std::abs(mf - previousM);
      const Real de = std::abs(allCells - previousError);
      const Real quotient = (dm > 0.0) ? (de / dm) : 0.0;
      const Real bound = (10.0 * dm) + 1e-12;
      std::printf("S %-10s m_f %.3e | error %.3e boundary %.3e | d_error %.3e bound %.3e "
                  "quotient %.3e %s%s\n",
                  label, mf, allCells, boundaryCells, de, bound, quotient,
                  de <= bound ? "PASS" : "FAIL", exact ? "" : " [BC NOT EXACT -- INVALID]");
    }
    previousError = allCells;
    previousBoundary = boundaryCells;
    previousM = mf;
  }
  (void)previousBoundary;
}

}  // namespace

int main() {
  std::printf("# P12-GRAD-002 diagnostics\n");
  reportFloors("2D 16^2 L=1 translated small",
               mesh::MeshGeometry::createStructuredQuad2D(
                   16, 16, cartesianVertices(16, 1.0, Vector3{0.005, 0.0025, 0.0})),
               1.0, 16);
  reportFloors("2D 16^2 L=1e-3 translated",
               mesh::MeshGeometry::createStructuredQuad2D(
                   16, 16, cartesianVertices(16, 1e-3, Vector3{5e-6, 2.5e-6, 0.0})),
               1e-3, 16);
  reportFloors("2D 16^2 L=1e3 translated",
               mesh::MeshGeometry::createStructuredQuad2D(
                   16, 16, cartesianVertices(16, 1e3, Vector3{5.0, 2.5, 0.0})),
               1e3, 16);
  reportFloors("2D 16^2 translated LARGE",
               mesh::MeshGeometry::createStructuredQuad2D(
                   16, 16, cartesianVertices(16, 1.0, Vector3{1234.5678, 987.6543, 0.0})),
               1.0, 16);
  reportFloors("2D 256^2 translated LARGE",
               mesh::MeshGeometry::createStructuredQuad2D(
                   256, 256, cartesianVertices(256, 1.0, Vector3{1234.5678, 987.6543, 0.0})),
               1.0, 256);
  reportFloors("2D 256^2 translated small",
               mesh::MeshGeometry::createStructuredQuad2D(
                   256, 256, cartesianVertices(256, 1.0, Vector3{0.005, 0.0025, 0.0})),
               1.0, 256);
  reportSweep("sweep");
  return 0;
}
