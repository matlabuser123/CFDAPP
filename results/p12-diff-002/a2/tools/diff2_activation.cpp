// P12-DIFF-002 Amendment A2 -- activation-consistency instrument.
//
// Question: does the selected Dirichlet WALL-FLUX SPATIAL SCHEME depend on
// solver.json's `non_orthogonal_corrections` (N)?  It must not (A2 §5).
//
// Two observables, both taken through the real production assemblers, so nothing here
// re-implements production's gating rule (which would make the gate circular):
//
//   (A) boundaryValueCoefficient, isolated EXACTLY.
//       Every assembler is linear in the prescribed boundary value, and the prescribed value enters
//       the row only as `rhs += boundaryValueCoefficient * phi_b`. So assembling twice with
//       boundary values V and V + delta and differencing gives, per row and exactly,
//           (rhs(V + delta) - rhs(V)) / delta  ==  sum of boundaryValueCoefficient over that row's
//                                                  value-prescribing boundary faces
//       with every internal-face contribution, every source and every diagonal cancelling. This
//       works on ANY geometry -- orthogonal or distorted -- and isolates the wall scheme alone.
//       The two-point form gives Gamma |S_orth| / d; DIFF-002 gives Gamma |S| (1/h1 + 1/h2). Those
//       differ by roughly a factor of 8/3 on a uniform grid, so the observable is not subtle.
//
//   (B) the whole assembled system, bitwise, on an ORTHOGONAL mesh.
//       P12-NUM-003's internal-face correction is documented and verified BIT-IDENTICAL on an
//       orthogonal mesh, so there the only thing N can possibly change is the boundary treatment.
//       Any difference across N is therefore attributable to the wall scheme, and after A2 the
//       systems must be bitwise identical for N = 0, 1, 2.
//
// Non-vacuity: run against the CURRENT (pre-A2) library this must REPORT A DIFFERENCE between
// N = 0 and N > 0 -- that is the negative control proving the instrument detects the live defect.
// After A2 it must report invariance. The same binary, unchanged, is used for both runs.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/species/SpeciesEquation.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// The production mapping from the case setting to the assembler options -- used, not reimplemented.
discretization::NonOrthogonalCorrectionOptions optionsFor(Index n) {
  pressure_velocity::SIMPLESettings settings;
  settings.nonOrthogonalCorrections = n;
  return pressure_velocity::nonOrthogonalOptions(settings);
}

bool sameBits(const algebra::Vector& a, const algebra::Vector& b) {
  if (a.size() != b.size()) return false;
  for (Index i = 0; i < a.size(); ++i) {
    const Real x = a[i];
    const Real y = b[i];
    if (std::memcmp(&x, &y, sizeof(Real)) != 0) return false;
  }
  return true;
}

bool sameBits(const algebra::SparseMatrix& a, const algebra::SparseMatrix& b) {
  if (a.rows() != b.rows() || a.nonZeros() != b.nonZeros()) return false;
  for (Index k = 0; k < a.nonZeros(); ++k) {
    if (a.columnIndicesData()[k] != b.columnIndicesData()[k]) return false;
    const Real x = a.valuesData()[k];
    const Real y = b.valuesData()[k];
    if (std::memcmp(&x, &y, sizeof(Real)) != 0) return false;
  }
  return true;
}

struct Assembled {
  algebra::SparseMatrix matrix;
  algebra::Vector rhs;
};

// ---- the four affected implicit callers, each driven through its public assembler ----

Assembled assembleThermal(const Mesh& mesh, Index n, Real wallValue) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedTemperature>(wallValue));
  }
  const fields::ScalarField temperature(mesh.numberOfCells(), 300.0);
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  thermal::assembleThermalDiffusionContribution(mesh, 0.6, temperature, bcs, builder, rhs,
                                                optionsFor(n));
  return {builder.build(), rhs};
}

Assembled assembleSpecies(const Mesh& mesh, Index n, Real wallValue) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedValue>(wallValue));
  }
  const fields::ScalarField concentration(mesh.numberOfCells(), 1.0);
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  species::assembleSpeciesDiffusionContribution(mesh, 998.0 * 1.0e-5, concentration, bcs, builder,
                                                rhs, optionsFor(n));
  return {builder.build(), rhs};
}

Assembled assembleKEpsilon(const Mesh& mesh, Index n, Real wallValue) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedValue>(wallValue));
  }
  const fields::ScalarField phi(mesh.numberOfCells(), 1.0);
  const fields::ScalarField gamma(mesh.numberOfCells(), 0.37);
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  turbulence::assembleScalarDiffusionContribution(mesh, gamma, phi, bcs, builder, rhs,
                                                  optionsFor(n));
  return {builder.build(), rhs};
}

Assembled assembleMomentum(const Mesh& mesh, Index n, Real wallValue) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::MovingWall>(Vector3{wallValue, 0.0, 0.0}));
  }
  const fields::VectorField velocity(mesh.numberOfCells(), Vector3{});
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  const auto options = optionsFor(n);
  physics::assembleDiffusionContribution(mesh, 1.0e-3, velocity, bcs, physics::VelocityComponent::U,
                                         builder, rhs, options.enabled, options.gradientScheme);
  return {builder.build(), rhs};
}

using Assembler = Assembled (*)(const Mesh&, Index, Real);

// Observable (A): the exact per-row boundaryValueCoefficient sum, by differencing the prescribed
// value. Linear in the boundary value, so this is exact, not a finite-difference approximation.
algebra::Vector boundaryValueCoefficients(Assembler assemble, const Mesh& mesh, Index n) {
  constexpr Real kBase = 2.0;
  constexpr Real kDelta = 1.0;
  const Assembled lo = assemble(mesh, n, kBase);
  const Assembled hi = assemble(mesh, n, kBase + kDelta);
  algebra::Vector out(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) out[i] = (hi.rhs[i] - lo.rhs[i]) / kDelta;
  return out;
}

Real maxAbsDiff(const algebra::Vector& a, const algebra::Vector& b) {
  Real worst = 0.0;
  for (Index i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
  return worst;
}

Real maxAbs(const algebra::Vector& a) {
  Real worst = 0.0;
  for (Index i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i]));
  return worst;
}

void report(const std::string& label, Assembler assemble, const Mesh& mesh, bool orthogonal) {
  // (A) wall-scheme invariance across N, on any geometry.
  const algebra::Vector c0 = boundaryValueCoefficients(assemble, mesh, 0);
  const algebra::Vector c1 = boundaryValueCoefficients(assemble, mesh, 1);
  const algebra::Vector c2 = boundaryValueCoefficients(assemble, mesh, 2);
  const bool bits01 = sameBits(c0, c1);
  const bool bits12 = sameBits(c1, c2);
  const Real d01 = maxAbsDiff(c0, c1);
  const Real d12 = maxAbsDiff(c1, c2);

  // (B) whole-system bitwise invariance -- meaningful only where the internal-face correction is
  // provably bit-identical, i.e. on an orthogonal mesh.
  std::string systemVerdict = "n/a (non-orthogonal: internal correction legitimately differs)";
  if (orthogonal) {
    const Assembled a0 = assemble(mesh, 0, 2.0);
    const Assembled a1 = assemble(mesh, 1, 2.0);
    const Assembled a2 = assemble(mesh, 2, 2.0);
    const bool s01 = sameBits(a0.matrix, a1.matrix) && sameBits(a0.rhs, a1.rhs);
    const bool s12 = sameBits(a1.matrix, a2.matrix) && sameBits(a1.rhs, a2.rhs);
    systemVerdict = std::string("N0==N1 ") + (s01 ? "BITWISE-SAME" : "DIFFERENT") + ", N1==N2 " +
                    (s12 ? "BITWISE-SAME" : "DIFFERENT");
  }

  std::printf("%-46s scale %.6e | bvc: N0==N1 %-13s (max|d| %.6e)  N1==N2 %-13s (max|d| %.6e) | "
              "system: %s\n",
              label.c_str(), maxAbs(c1), bits01 ? "BITWISE-SAME" : "DIFFERENT", d01,
              bits12 ? "BITWISE-SAME" : "DIFFERENT", d12, systemVerdict.c_str());
}

Mesh distortedMesh(Index n, Real shear) {
  // Interior nodes only are displaced, so the boundary patches stay exactly planar and the
  // prescribed boundary values stay exact (the GRAD-002 instrument lesson).
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      const bool interior = (i > 0 && i < n && j > 0 && j < n);
      vertices.push_back(Vector3{xi + (interior ? shear * eta / static_cast<Real>(n) : 0.0),
                                 eta + (interior ? shear * xi / static_cast<Real>(n) : 0.0), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002 A2 activation consistency: does the Dirichlet wall-flux SCHEME\n");
  std::printf("# depend on solver.json non_orthogonal_corrections (N = 0, 1, 2)?  It must not.\n");
  std::printf("# bvc = the exact per-row boundaryValueCoefficient, isolated by differencing the\n");
  std::printf("# prescribed boundary value (assembly is linear in it, so this is exact).\n");
  std::printf("# REQUIRED AFTER A2: every 'N0==N1' and 'N1==N2' reads BITWISE-SAME.\n");
  std::printf("# NEGATIVE CONTROL (pre-A2): 'N0==N1' must read DIFFERENT somewhere, or the\n");
  std::printf("# instrument cannot detect the live activation defect.\n\n");

  const Mesh cartesian2D = MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
  const Mesh cartesian3D = MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
  const Mesh graded = MeshGeometry::createGraded2D(
      16, 16, 1.0, 1.0,
      mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both},
      mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both});
  const Mesh distorted = distortedMesh(16, 0.45);

  struct Caller {
    const char* name;
    Assembler fn;
  };
  const Caller callers[] = {{"thermal", &assembleThermal},
                            {"species", &assembleSpecies},
                            {"k-epsilon", &assembleKEpsilon},
                            {"momentum", &assembleMomentum}};

  for (const Caller& c : callers) {
    const std::string name = c.name;
    std::printf("## %s\n", c.name);
    report(name + "  orthogonal Cartesian 16x16", c.fn, cartesian2D, true);
    report(name + "  orthogonal Cartesian 3D 8x8x8", c.fn, cartesian3D, true);
    report(name + "  orthogonal graded 16x16 r=1.2", c.fn, graded, true);
    report(name + "  DISTORTED 16x16 shear 0.45", c.fn, distorted, false);
    std::printf("\n");
  }
  return 0;
}
