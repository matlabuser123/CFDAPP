// P12-DIFF-002 ThermalInterface fix -- the regression reproducer, written BEFORE the production fix
// and kept as the permanent guard.
//
// The invariant under test: for a SINGLE region, the conjugate (region-aware) conduction assembly
// must be identical to the single-material assembly. `ThermalInterface.cpp`'s own comment states
// "Same boundary treatment as the single-material assembly", and `ThermalSolver` exposes both paths
// for the same physics, so a one-region conjugate solve is the same problem.
//
// P12-DIFF-002 A2 broke it: it made EnergyEquation's Dirichlet wall flux the DIFF-002 three-point
// reconstruction unconditionally, while the conjugate path kept the hard-coded two-point
// `conductivity * face.area() / distance` with no boundaryValueCoefficient, no far-cell entry and
// no explicit correction. Before the fix these tests failed with O(1) physical differences --
// solved temperature apart by 3.02 K (2x1), 0.66 K (10x10, 20x20) and 1.43 K (3D 8^3), recorded in
// results/p12-diff-002/investigation-f/logs/12_conjugate_divergence.log and in the frozen gate
// results/p12-diff-002/thermal-interface-fix/acceptance_gate.md section 1.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalInterface.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalRegion;
using cfd::thermal::ThermalRegionMap;
using cfd::thermal::ThermalRegionType;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;

constexpr Real kConductivity = 2.5;

BoundaryConditionSet dirichletWalls(const Mesh& mesh) {
  BoundaryConditionSet bcs;
  Real value = 300.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedTemperature>(value));
    value += 20.0;
  }
  return bcs;
}

// One region covering the whole mesh -- the configuration in which the two paths must agree.
ThermalRegionMap singleRegion(const Mesh& mesh) {
  return ThermalRegionMap(
      {ThermalRegion{"a", ThermalRegionType::Solid, ThermalProperties(kConductivity, 1.0)}},
      std::vector<Index>(mesh.numberOfCells(), 0));
}

Real storedEntry(const cfd::algebra::SparseMatrix& m, Index row, Index column) {
  const Index* offsets = m.rowOffsetsData();
  for (Index k = offsets[row]; k < offsets[row + 1]; ++k) {
    if (m.columnIndicesData()[k] == column) return m.valuesData()[k];
  }
  return 0.0;
}

// Both assemblies for the same one-region problem, compared entry by entry.
struct Comparison {
  std::size_t rowsDiffering{};
  Real worstMatrix{};
  Real worstRhs{};
  Real worstTemperature{};
  Real worstBoundaryFlux{};
};

Comparison compare(const Mesh& mesh) {
  const auto bcs = dirichletWalls(mesh);
  const ScalarField temperature(mesh.numberOfCells(), 305.0);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const auto regions = singleRegion(mesh);

  cfd::algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  cfd::algebra::Vector rhs(mesh.numberOfCells(), 0.0);
  cfd::thermal::assembleThermalDiffusionContribution(mesh, kConductivity, temperature, bcs, builder,
                                                     rhs);
  const cfd::algebra::SparseMatrix single = builder.build();

  const auto conjugate =
      cfd::thermal::assembleConjugateConductionEquation(mesh, temperature, regions, bcs);

  Comparison c;
  for (Index row = 0; row < mesh.numberOfCells(); ++row) {
    bool differs = false;
    // Every column either assembly touches.
    for (Index col = 0; col < mesh.numberOfCells(); ++col) {
      const Real a = storedEntry(single, row, col);
      const Real b = storedEntry(conjugate.system.matrix(), row, col);
      if (a != b) differs = true;
      c.worstMatrix = std::max(c.worstMatrix, std::abs(a - b));
    }
    const Real dr = std::abs(rhs[row] - conjugate.system.rhs()[row]);
    if (dr != 0.0) differs = true;
    c.worstRhs = std::max(c.worstRhs, dr);
    if (differs) ++c.rowsDiffering;
  }

  const ThermalResult a =
      ThermalSolver{}.solve(mesh, temperature, noFlow, ThermalProperties(kConductivity, 1.0), bcs);
  const ThermalResult b = ThermalSolver{}.solveConjugateConduction(mesh, temperature, regions, bcs);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    c.worstTemperature =
        std::max(c.worstTemperature, std::abs(a.temperature[i] - b.temperature[i]));
  }

  // Total Dirichlet boundary flux implied by each assembled system: for row P the assembled
  // equation is (A T)_P = b_P, so the boundary contribution to that row is b_P minus the internal
  // part. Comparing (A T - b) row-sums between the two assemblies on the SAME field compares the
  // boundary flux each one represents, without re-deriving a per-face formula here.
  cfd::algebra::Vector t(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) t[i] = temperature[i];
  const cfd::algebra::Vector ra = single.multiply(t);
  const cfd::algebra::Vector rb = conjugate.system.matrix().multiply(t);
  Real fa = 0.0, fb = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    fa += ra[i] - rhs[i];
    fb += rb[i] - conjugate.system.rhs()[i];
  }
  c.worstBoundaryFlux = std::abs(fa - fb) / std::max(std::abs(fa), 1e-300);
  return c;
}

Mesh gradedMesh() {
  return MeshGeometry::createGraded2D(12, 12, 1.0, 1.0,
                                      cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.2,
                                                             cfd::mesh::GradingCluster::Both},
                                      cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.2,
                                                             cfd::mesh::GradingCluster::Both});
}

Mesh distortedMesh() {
  constexpr Index n = 12;
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      // Interior vertices only, so the boundary patches stay exactly planar.
      const bool interior = (i > 0 && i < n && j > 0 && j < n);
      vertices.push_back(Vector3{xi + (interior ? 0.45 * eta / static_cast<Real>(n) : 0.0),
                                 eta + (interior ? 0.45 * xi / static_cast<Real>(n) : 0.0), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

void expectPathsIdentical(const std::string& label, const Mesh& mesh) {
  const Comparison c = compare(mesh);
  std::printf(
      "%-26s rows differing %4zu | max |d matrix| %.6e  max |d rhs| %.6e |"
      " max |dT| %.6e | boundary-flux rel diff %.3e\n",
      label.c_str(), c.rowsDiffering, c.worstMatrix, c.worstRhs, c.worstTemperature,
      c.worstBoundaryFlux);
  // T2: the two assemblies call the same production helper with the same inputs, so the matrix and
  // RHS must be BITWISE identical -- no tolerance.
  EXPECT_EQ(c.rowsDiffering, 0u) << label << ": conjugate and single-material assemblies differ";
  EXPECT_EQ(c.worstMatrix, 0.0) << label;
  EXPECT_EQ(c.worstRhs, 0.0) << label;
  // T3: solved temperature at the existing test's own bound, and the boundary flux each system
  // represents.
  EXPECT_LE(c.worstTemperature, 1e-12) << label;
  EXPECT_LE(c.worstBoundaryFlux, 1e-12) << label;
}

}  // namespace

// T2/T3/T4 on the four reproducer meshes from the frozen gate section 1.
TEST(ConjugateBoundaryEquivalence, SingleRegionMatchesSingleMaterialOn2x1) {
  expectPathsIdentical("Cartesian 2x1", MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0));
}

TEST(ConjugateBoundaryEquivalence, SingleRegionMatchesSingleMaterialOn10x10) {
  expectPathsIdentical("Cartesian 10x10", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0));
}

TEST(ConjugateBoundaryEquivalence, SingleRegionMatchesSingleMaterialOn20x20) {
  expectPathsIdentical("Cartesian 20x20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0));
}

TEST(ConjugateBoundaryEquivalence, SingleRegionMatchesSingleMaterialIn3D) {
  expectPathsIdentical("Cartesian 3D 8x8x8",
                       MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
}

// T4's remaining two geometries: the reconstruction's coefficients vary with cell spacing, and the
// tangential transfer term is non-zero only on a non-orthogonal mesh, so both must be covered.
TEST(ConjugateBoundaryEquivalence, SingleRegionMatchesSingleMaterialOnAGradedMesh) {
  expectPathsIdentical("graded 12x12 r=1.2", gradedMesh());
}

TEST(ConjugateBoundaryEquivalence, SingleRegionMatchesSingleMaterialOnADistortedMesh) {
  expectPathsIdentical("distorted 12x12 s=0.45", distortedMesh());
}

// T6: one-cell-thick directions, where the boundary faces normal to the thin direction have no
// valid opposite interior stencil and must take the historical two-point fallback in BOTH paths.
TEST(ConjugateBoundaryEquivalence, SingleRegionMatchesSingleMaterialOnOneCellThickMeshes) {
  expectPathsIdentical("Cartesian 1x1", MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0));
  expectPathsIdentical("Cartesian 8x1", MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0));
  expectPathsIdentical("Cartesian 1x8", MeshGeometry::createCartesian2D(1, 8, 1.0, 8.0));
  expectPathsIdentical("Cartesian 3D 1x1x1",
                       MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, 1.0));
  expectPathsIdentical("Cartesian 3D 8x8x1",
                       MeshGeometry::createCartesian3D(8, 8, 1, 8.0, 8.0, 1.0));
}
