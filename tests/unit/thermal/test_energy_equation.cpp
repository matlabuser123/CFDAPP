#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalProperties.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::FixedValue;
using cfd::boundary::HeatFlux;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::assembleEnergyEquation;
using cfd::thermal::assembleThermalConvectionContribution;
using cfd::thermal::assembleThermalDiffusionContribution;
using cfd::thermal::assembleThermalSourceContribution;
using cfd::thermal::EnergyAssembly;
using cfd::thermal::ThermalProperties;

namespace {

// A 2-cell horizontal channel: cell 0 and cell 1 share one internal face;
// each cell also touches exactly 3 boundary faces (left-or-right, top,
// bottom). dx = dy = 1.0, so every boundary face has area=1.0,
// distance=0.5 (conductance 2k), and the internal face has area=1.0,
// distance=1.0 (conductance k) -- exact, hand-computable numbers reused
// across several tests below.
Mesh makeTwoCellMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

BoundaryConditionSet makeFixedTemperatureBoundaries(const Mesh& mesh, Real left, Real right,
                                                    Real top, Real bottom) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(left));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(right));
  boundaries.set(mesh, "top", std::make_unique<FixedValue>(top));
  boundaries.set(mesh, "bottom", std::make_unique<FixedValue>(bottom));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

Index internalFaceId(const Mesh& mesh) {
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) return face.id();
  }
  ADD_FAILURE() << "mesh has no internal face";
  return 0;
}

// The single face's owner cell for a patch with exactly one face (true for
// "left"/"right" on the 2x1 mesh above).
Index cellTouchingPatch(const Mesh& mesh, std::string_view patchName) {
  return mesh.face(mesh.boundaryPatch(patchName).faceIds().front()).owner();
}

}  // namespace

// ---------------------------------------------------------------------
// A. Two-cell diffusion system (hand-derived).
// ---------------------------------------------------------------------
TEST(EnergyEquationDiffusionTest, TwoCellSystemMatchesHandDerivedCoefficients) {
  // P12-DIFF-002 A5, entry 2 (validation-migration/acceptance_gate_A5.md). The Dirichlet wall flux
  // is the second-order one-sided reconstruction, so a value-prescribing wall is no longer k|S|/d.
  // Re-derived by hand below and independently in a5/tools/derive_expected.py; the mesh is
  // 2 x 1 cells on 2.0 x 1.0, so h = 1 in both directions, every face area is 1 (unit depth), k
  // = 3.
  //
  // left/right walls -- the x axis has 2 cells, so a second cell exists inward and the
  // reconstruction applies, with h1 = h/2 = 0.5 and h2 = 3h/2 = 1.5:
  //   cP = k|S| h2 / (h1 (h2 - h1)) = 3 * 1.5 / (0.5 * 1.0) = 9
  //   cF = k|S| h1 / (h2 (h2 - h1)) = 3 * 0.5 / (1.5 * 1.0) = 1
  //   cB = k|S| (1/h1 + 1/h2)       = 3 * (2 + 2/3)         = 8      and cP - cF = cB
  // top/bottom walls -- normal to the ONE-cell-thick y axis, so no inward stencil exists and they
  // keep the historical two-point form exactly: k|S|/h1 = 3 * 1.0 / 0.5 = 6.
  // internal face: k|S|/dPN = 3 * 1.0 / 1.0 = 3, untouched by DIFF-002.
  //
  // FixedValue ignores the owner field value, so the temperature field's contents are irrelevant.
  const Mesh mesh = makeTwoCellMesh();
  const Real k = 3.0;
  const Real tLeft = 10.0, tRight = 20.0, tTop = 15.0, tBottom = 5.0;
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, tLeft, tRight, tTop, tBottom);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, k, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Index cellA = mesh.face(internalFaceId(mesh)).owner();
  const Index cellB = *mesh.face(internalFaceId(mesh)).neighbor();
  const Index leftCell = cellTouchingPatch(mesh, "left");
  const Index rightCell = cellTouchingPatch(mesh, "right");

  const Real cP = 9.0;                   // reconstructed x-wall, owner coefficient
  const Real cF = 1.0;                   // reconstructed x-wall, far-cell coefficient
  const Real cB = 8.0;                   // reconstructed x-wall, prescribed-value multiplier
  const Real fallbackConductance = 6.0;  // y-walls, one cell thick: k*A/h1 = 3*1.0/0.5
  const Real internalConductance = 3.0;  // k*A/dPN = 3*1.0/1.0
  // The reconstruction's own analytic identity, so the three constants above cannot drift apart.
  ASSERT_DOUBLE_EQ(cP - cF, cB);
  const Real expectedDiagonal = cP + 2.0 * fallbackConductance + internalConductance;  // 24

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellB], expectedDiagonal, 1e-10);
  // Each cell's x-wall reaches its far cell through the shared internal face, so the far-cell
  // coefficient lands on the same entry as the internal coupling: -(3 + 1) = -4.
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -(internalConductance + cF), 1e-10);
  EXPECT_NEAR(matrix.multiply(eA)[cellB], -(internalConductance + cF), 1e-10);

  const Real commonRhs = fallbackConductance * tTop + fallbackConductance * tBottom;
  const Real expectedRhsLeft = cB * tLeft + commonRhs;    // 8*10 + 6*15 + 6*5 = 200
  const Real expectedRhsRight = cB * tRight + commonRhs;  // 8*20 + 6*15 + 6*5 = 280
  EXPECT_NEAR(rhs[leftCell], expectedRhsLeft, 1e-9);
  EXPECT_NEAR(rhs[rightCell], expectedRhsRight, 1e-9);
}

// ---------------------------------------------------------------------
// B. Diffusion symmetry (conservative equal/opposite internal coupling).
// ---------------------------------------------------------------------
TEST(EnergyEquationDiffusionTest, InternalFaceCoefficientsAreSymmetric) {
  // P12-DIFF-002 A5, entry 3 (validation-migration/acceptance_gate_A5.md, class M-A/2). The
  // property under test -- an internal face contributes equally and oppositely to its two rows --
  // is unchanged, and is still asserted below. What changed is that A(0,1) is no longer a pure
  // internal coupling: cell 0's xmin wall reaches its far cell THROUGH the face it shares with
  // cell 1, so the one-sided far-cell coefficient lands on that same matrix entry. Cell 1 is in the
  // middle column and has no x-normal wall, so A(1,0) remains a pure internal coupling.
  //
  // That entry-level asymmetry is deliberate (results/p12-diff-002/architecture.md section 2 -- it
  // is why every equation using this treatment is solved with BiCGSTAB and never CG) and is
  // asserted directly by BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne. It
  // does NOT weaken conservation, which telescopes over faces and is covered by
  // ThermalBoundaryConsistency.
  //
  // Derived independently (a5/tools/derive_expected.py, block D): h = 1/3, |S| = 1/3, k = 1, so
  //   cInt = k|S|/dPN = 1        cF = k|S| h1 / (h2 (h2 - h1)) = 1/3
  //   A(1,0) = -cInt = -1        A(0,1) = -(cInt + cF) = -4/3       A(0,1) - A(1,0) = -cF
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, 1.0, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Real cInt = 1.0;      // k |S| / dPN = 1 * (1/3) / (1/3)
  const Real cF = 1.0 / 3.0;  // k |S| h1 / (h2 (h2 - h1)), h1 = 1/6, h2 = 1/2, |S| = 1/3

  Vector e0(n, 0.0);
  e0[0] = 1.0;
  Vector e1(n, 0.0);
  e1[1] = 1.0;
  const Real a10 = matrix.multiply(e0)[1];  // A(1,0) -- pure internal coupling
  const Real a01 = matrix.multiply(e1)[0];  // A(0,1) -- internal coupling + cell 0's far-cell term
  EXPECT_NEAR(a10, -cInt, 1e-12);
  EXPECT_NEAR(a01, -(cInt + cF), 1e-12);
  // The one-sided far-cell coefficient is the ONLY thing that breaks entry-level symmetry here.
  EXPECT_NEAR(a01 - a10, -cF, 1e-12);
  EXPECT_LT(a01, 0.0);
  EXPECT_LT(a10, 0.0);
  EXPECT_TRUE(matrix.allFinite());

  // The equal/opposite internal-face property, tested where no far-cell term can reach.
  //
  // (a) Between two INTERIOR cells. On a 5x5 mesh cells 6 = (1,1) and 7 = (2,1) have no boundary
  //     face at all, so neither row receives a far-cell entry and their coupling must be exactly
  //     symmetric.
  {
    const Mesh wide = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
    const auto wideBcs = makeFixedTemperatureBoundaries(wide, 0.0, 0.0, 0.0, 0.0);
    const Index m = wide.numberOfCells();
    const ScalarField t(m, 0.0);
    SparseMatrixBuilder b(m, m);
    Vector r(m, 0.0);
    assembleThermalDiffusionContribution(wide, 1.0, t, wideBcs, b, r);
    const auto wideMatrix = b.build();
    Vector e6(m, 0.0);
    e6[6] = 1.0;
    Vector e7(m, 0.0);
    e7[7] = 1.0;
    EXPECT_DOUBLE_EQ(wideMatrix.multiply(e6)[7], wideMatrix.multiply(e7)[6]);
    EXPECT_LT(wideMatrix.multiply(e6)[7], 0.0);
  }

  // (b) Over EVERY internal face at once: with gradient-type boundaries no wall is ever
  //     reconstructed, so no far-cell entry exists anywhere and the whole off-diagonal structure
  //     must be exactly symmetric.
  {
    const auto neumann = makeZeroGradientBoundaries(mesh);
    SparseMatrixBuilder b(n, n);
    Vector r(n, 0.0);
    assembleThermalDiffusionContribution(mesh, 1.0, temperature, neumann, b, r);
    const auto sym = b.build();
    std::size_t offDiagonalsChecked = 0;
    for (Index row = 0; row < n; ++row) {
      Vector eRow(n, 0.0);
      eRow[row] = 1.0;
      const auto column = sym.multiply(eRow);
      for (Index col = 0; col < n; ++col) {
        if (col == row) continue;
        Vector eCol(n, 0.0);
        eCol[col] = 1.0;
        EXPECT_DOUBLE_EQ(column[col], sym.multiply(eCol)[row]) << row << "," << col;
        if (column[col] != 0.0) ++offDiagonalsChecked;
      }
    }
    EXPECT_EQ(offDiagonalsChecked, 24u);  // 12 internal faces on a 3x3 mesh, two entries each
  }
}

// ---------------------------------------------------------------------
// C. Constant-temperature null mode.
// ---------------------------------------------------------------------
TEST(EnergyEquationDiffusionTest, ConstantTemperatureGivesZeroContributionEverywhere) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real value = 7.0;
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, value, value, value, value);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, value);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, 2.0, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Vector tExact(n, value);
  const Vector residual = matrix.multiply(tExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-10);
  }
}

TEST(EnergyEquationDiffusionTest, DiagonalIsFinitePositiveAndNonzero) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, 1.5, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  for (Index row = 0; row < n; ++row) {
    const Real aP = matrix.diagonal(row);
    EXPECT_TRUE(std::isfinite(aP));
    EXPECT_GT(aP, 0.0);
  }
}

TEST(EnergyEquationDiffusionTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const ScalarField temperature(mesh.numberOfCells() + 1, 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(
      assembleThermalDiffusionContribution(mesh, 1.0, temperature, boundaries, builder, rhs),
      InvalidArgumentError);
}

// ---------------------------------------------------------------------
// D. Upwind convection direction.
// ---------------------------------------------------------------------
TEST(EnergyEquationConvectionTest, PositiveInternalFluxSelectsOwnerNegativeSelectsNeighbor) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  const Real cp = 1.0;
  const Index faceId = internalFaceId(mesh);
  const Index owner = mesh.face(faceId).owner();
  const Index neighbor = *mesh.face(faceId).neighbor();

  {
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    massFlux[faceId] = 5.0;  // F>=0: owner upwind.
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleThermalConvectionContribution(mesh, cp, massFlux, temperature, boundaries, builder,
                                          rhs);
    const auto matrix = builder.build();
    Vector eOwner(n, 0.0);
    eOwner[owner] = 1.0;
    EXPECT_NEAR(matrix.multiply(eOwner)[owner], 5.0, 1e-12);      // A(owner,owner) += cp*F
    EXPECT_NEAR(matrix.multiply(eOwner)[neighbor], -5.0, 1e-12);  // A(neighbor,owner) -= cp*F
  }
  {
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    massFlux[faceId] = -5.0;  // F<0: neighbor upwind.
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleThermalConvectionContribution(mesh, cp, massFlux, temperature, boundaries, builder,
                                          rhs);
    const auto matrix = builder.build();
    Vector eNeighbor(n, 0.0);
    eNeighbor[neighbor] = 1.0;
    EXPECT_NEAR(matrix.multiply(eNeighbor)[owner], -5.0, 1e-12);  // A(owner,neighbor) += cp*F (F<0)
    EXPECT_NEAR(matrix.multiply(eNeighbor)[neighbor], 5.0, 1e-12);  // A(neighbor,neighbor) -= cp*F
  }
}

TEST(EnergyEquationConvectionTest, BoundaryOutflowUsesOwnerUnknownNotBoundaryValue) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 100.0, 200.0, 300.0, 400.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  const Real cp = 2.0;

  const Index rightFaceId = mesh.boundaryPatch("right").faceIds().front();
  const Index ownerCell = mesh.face(rightFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[rightFaceId] = 4.0;  // outflow

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalConvectionContribution(mesh, cp, massFlux, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], cp * 4.0, 1e-12);
  EXPECT_NEAR(rhs[ownerCell], 0.0, 1e-12);
}

TEST(EnergyEquationConvectionTest, BoundaryInflowUsesBoundaryValueOnRhs) {
  const Mesh mesh = makeTwoCellMesh();
  const Real tLeft = 12.0;
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, tLeft, 200.0, 300.0, 400.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  const Real cp = 2.0;

  const Index leftFaceId = mesh.boundaryPatch("left").faceIds().front();
  const Index ownerCell = mesh.face(leftFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[leftFaceId] = -3.0;  // inflow

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalConvectionContribution(mesh, cp, massFlux, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], 0.0, 1e-12);
  // -effectiveFlux*tB = -(cp*-3)*tLeft = cp*3*tLeft
  EXPECT_NEAR(rhs[ownerCell], cp * 3.0 * tLeft, 1e-12);
}

// Conservation (spec section 11): a divergence-free flux field with a
// constant temperature must give exactly zero net convective
// contribution -- the transported energy leaving one cell's row is
// exactly cancelled by what enters the neighbor's, never double-counted.
TEST(EnergyEquationConvectionTest,
     ConstantTemperatureWithDivergenceFreeFluxGivesZeroNetContribution) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real value = 9.0;
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, value, value, value, value);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, value);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);  // divergence-free: zero everywhere.

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalConvectionContribution(mesh, 4.0, massFlux, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();
  const Vector tExact(n, value);
  const Vector residual = matrix.multiply(tExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-12);
  }
}

TEST(EnergyEquationConvectionTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells() + 1, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(assembleThermalConvectionContribution(mesh, 1.0, massFlux, temperature, boundaries,
                                                     builder, rhs),
               InvalidArgumentError);
}

TEST(EnergyEquationConvectionTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(assembleThermalConvectionContribution(mesh, 1.0, massFlux, temperature, boundaries,
                                                     builder, rhs),
               InvalidArgumentError);
}

// ---------------------------------------------------------------------
// E. Heat-source RHS.
// ---------------------------------------------------------------------
TEST(EnergyEquationSourceTest, AddsQTimesVolumeExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 2.0, 2.0);
  const Index n = mesh.numberOfCells();
  Vector rhs(n, 0.0);
  const Real q = 12.0;
  assembleThermalSourceContribution(mesh, q, rhs);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(rhs[cell.id()], q * cell.volume(), 1e-12);
  }
}

TEST(EnergyEquationSourceTest, AccumulatesOnExistingRhs) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  Vector rhs(n, 1.0);  // pre-existing contribution from another term.
  const Real q = 3.0;
  assembleThermalSourceContribution(mesh, q, rhs);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(rhs[cell.id()], 1.0 + q * cell.volume(), 1e-12);
  }
}

TEST(EnergyEquationSourceTest, RejectsNonFiniteHeatSource) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  Vector rhs(mesh.numberOfCells(), 0.0);
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(assembleThermalSourceContribution(mesh, nan, rhs), InvalidArgumentError);
  EXPECT_THROW(assembleThermalSourceContribution(mesh, inf, rhs), InvalidArgumentError);
}

// ---------------------------------------------------------------------
// F. Combined assembly (diffusion + convection + source, hand-derived).
// ---------------------------------------------------------------------
TEST(EnergyEquationAssemblyTest, CombinedAssemblyMatchesHandDerivedCoefficients) {
  const Mesh mesh = makeTwoCellMesh();
  const Real k = 3.0;
  const Real cp = 2.0;
  const Real tLeft = 10.0, tRight = 20.0, tTop = 15.0, tBottom = 5.0;
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, tLeft, tRight, tTop, tBottom);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);  // FixedValue ignores this.
  const ThermalProperties thermal(k, cp);

  const Index faceId = internalFaceId(mesh);
  const Index cellA = mesh.face(faceId).owner();  // upwind cell for F>=0.
  const Index cellB = *mesh.face(faceId).neighbor();
  const Real fluxAtoB = 4.0;
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[faceId] = fluxAtoB;

  const Real q = 6.0;
  const EnergyAssembly assembly =
      assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries, q);
  const auto& matrix = assembly.system.matrix();
  const auto& rhs = assembly.system.rhs();

  // P12-DIFF-002 A5, entry 4. Same wall reconstruction as
  // EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients above (k = 3, h = 1,
  // |S| = 1 -> cP = 9, cF = 1, cB = 8; the one-cell-thick y walls keep the two-point 6). The
  // convection and source terms are untouched by DIFF-002. Derived independently in
  // a5/tools/derive_expected.py, block A2.
  const Real cP = 9.0;
  const Real cF = 1.0;
  const Real cB = 8.0;
  const Real fallbackConductance = 6.0;   // y walls
  const Real internalConductance = 3.0;   // k*A/dPN
  const Real convection = cp * fluxAtoB;  // 8.0
  ASSERT_DOUBLE_EQ(cP - cF, cB);
  const Real diagonalBase = cP + 2.0 * fallbackConductance + internalConductance;  // 24

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], diagonalBase + convection, 1e-9);  // cellA upwind
  EXPECT_NEAR(matrix.multiply(eB)[cellB], diagonalBase, 1e-9);               // cellB not upwind
  // Diffusion only, but now carrying the far-cell coefficient on the same entry.
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -(internalConductance + cF), 1e-9);
  EXPECT_NEAR(matrix.multiply(eA)[cellB], -(internalConductance + cF) - convection, 1e-9);

  const Index leftCell = cellTouchingPatch(mesh, "left");
  const Index rightCell = cellTouchingPatch(mesh, "right");
  const Real commonRhs = fallbackConductance * tTop + fallbackConductance * tBottom;
  const Real volume = mesh.cell(0).volume();  // both cells are unit squares here.
  EXPECT_NEAR(rhs[leftCell], cB * tLeft + commonRhs + q * volume, 1e-9);    // 206
  EXPECT_NEAR(rhs[rightCell], cB * tRight + commonRhs + q * volume, 1e-9);  // 286

  EXPECT_TRUE(matrix.allFinite());
  EXPECT_TRUE(rhs.allFinite());
  for (Index row = 0; row < n; ++row) {
    EXPECT_GT(assembly.diagonal[row], 0.0);
  }
}

TEST(EnergyEquationAssemblyTest, ProducesCorrectDimensions) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 3, 2.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  const EnergyAssembly assembly =
      assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries);

  EXPECT_EQ(assembly.system.matrix().rows(), n);
  EXPECT_EQ(assembly.system.matrix().columns(), n);
  EXPECT_EQ(assembly.system.rhs().size(), n);
  EXPECT_EQ(assembly.diagonal.size(), n);
}

TEST(EnergyEquationAssemblyTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells() + 1, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(1.0, 1.0);

  EXPECT_THROW((void)assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries),
               InvalidArgumentError);
}

TEST(EnergyEquationAssemblyTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  const ThermalProperties thermal(1.0, 1.0);

  EXPECT_THROW((void)assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries),
               InvalidArgumentError);
}

TEST(EnergyEquationAssemblyTest, RejectsNonFiniteHeatSource) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells(), 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(1.0, 1.0);
  const Real inf = std::numeric_limits<Real>::infinity();

  EXPECT_THROW((void)assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries, inf),
               InvalidArgumentError);
}

TEST(EnergyEquationAssemblyTest, NonFiniteTemperatureAtGradientBoundaryThrowsNumericalError) {
  // FixedGradient reconstructs its face value from the *owner* temperature
  // (unlike FixedValue, which ignores it). Since P12-MESH-003 the diffusion
  // term no longer needs it (a gradient-type face contributes its exact
  // prescribed flux, boundaryDiffusionContribution), but fluid entering
  // through such a face carries that reconstructed value -- so a
  // non-finite owner value with inflow on its gradient face propagates
  // into the assembled RHS.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  ScalarField temperature(mesh.numberOfCells(), 300.0);
  temperature[0] = std::numeric_limits<Real>::quiet_NaN();
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary() && face.owner() == 0) massFlux[face.id()] = -1.0;  // inflow
  }
  const ThermalProperties thermal(1.0, 1.0);

  EXPECT_THROW((void)assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries),
               NumericalError);
}

TEST(EnergyEquationAssemblyTest, NonFiniteMassFluxThrowsInvalidArgumentError) {
  // A non-finite massFlux entry produces a non-finite *matrix* coefficient
  // (via assembleThermalConvectionContribution's effectiveFlux), and
  // algebra::SparseMatrix's own constructor already rejects non-finite
  // values at builder.build() time -- so this surfaces as
  // InvalidArgumentError from the matrix layer itself, before
  // assembleEnergyEquation's own post-hoc finite check ever runs (that
  // check's matrix.allFinite() branch is therefore unreachable in
  // practice, same as physics::MomentumEquation's identical pattern).
  // Contrast with NonFiniteTemperatureAtGradientBoundaryThrowsNumericalError
  // above: a non-finite value that lands only in the RHS *is* caught by
  // that post-hoc check, since algebra::Vector does not self-validate.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField temperature(mesh.numberOfCells(), 300.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[internalFaceId(mesh)] = std::numeric_limits<Real>::infinity();
  const ThermalProperties thermal(1.0, 1.0);

  EXPECT_THROW((void)assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries),
               InvalidArgumentError);
}

TEST(EnergyEquationAssemblyTest, RepeatedAssemblyIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 350.0, 300.0, 320.0, 320.0);
  const Index n = mesh.numberOfCells();
  ScalarField temperature(n, 310.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.7;
  }
  const ThermalProperties thermal(0.6, 4180.0);

  const EnergyAssembly a =
      assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries, 2.0);
  const EnergyAssembly b =
      assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries, 2.0);

  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.system.rhs()[i], b.system.rhs()[i]);
    EXPECT_EQ(a.diagonal[i], b.diagonal[i]);
  }
}

// ---------------------------------------------------------------------
// P2-THERMAL-003 integration: assembleEnergyEquation has no special-case
// logic for any particular BoundaryCondition subtype (it only ever calls
// the polymorphic ScalarBoundaryCondition::boundaryValue), so the real
// thermal BC classes (FixedTemperature/HeatFlux/Adiabatic) plug in
// without any EnergyEquation.cpp changes -- these tests prove that
// numerically, not just that it compiles.
// ---------------------------------------------------------------------

TEST(EnergyEquationThermalBCIntegrationTest,
     FixedTemperatureGivesIdenticalResultToEquivalentFixedValue) {
  // Same setup/numbers as
  // EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients,
  // but using the thermal-specific FixedTemperature class instead of the
  // generic FixedValue -- must reproduce the exact same hand-derived
  // coefficients, since the two are mathematically identical.
  const Mesh mesh = makeTwoCellMesh();
  const Real k = 3.0;
  const Real tLeft = 10.0, tRight = 20.0, tTop = 15.0, tBottom = 5.0;
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(tLeft));
  boundaries.set(mesh, "right", std::make_unique<FixedTemperature>(tRight));
  boundaries.set(mesh, "top", std::make_unique<FixedTemperature>(tTop));
  boundaries.set(mesh, "bottom", std::make_unique<FixedTemperature>(tBottom));
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, k, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Index cellA = mesh.face(internalFaceId(mesh)).owner();
  const Index cellB = *mesh.face(internalFaceId(mesh)).neighbor();
  const Index leftCell = cellTouchingPatch(mesh, "left");
  const Index rightCell = cellTouchingPatch(mesh, "right");

  // P12-DIFF-002 A5, entry 5. Same reconstruction as
  // EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients (cP = 9, cF = 1,
  // cB = 8; one-cell-thick y walls keep the two-point 6); derived in a5/tools/derive_expected.py.
  const Real cP = 9.0;
  const Real cF = 1.0;
  const Real cB = 8.0;
  const Real fallbackConductance = 6.0;
  const Real internalConductance = 3.0;
  ASSERT_DOUBLE_EQ(cP - cF, cB);
  const Real expectedDiagonal = cP + 2.0 * fallbackConductance + internalConductance;  // 24

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellB], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -(internalConductance + cF), 1e-10);

  const Real commonRhs = fallbackConductance * tTop + fallbackConductance * tBottom;
  EXPECT_NEAR(rhs[leftCell], cB * tLeft + commonRhs, 1e-9);    // 200
  EXPECT_NEAR(rhs[rightCell], cB * tRight + commonRhs, 1e-9);  // 280

  // A5 strengthening: this test is named for the equivalence of FixedTemperature and FixedValue but
  // only ever re-checked the same constants. Assert the equivalence itself, bitwise -- a property
  // that holds under any wall operator and would have survived the A2 change untouched.
  SparseMatrixBuilder genericBuilder(n, n);
  Vector genericRhs(n, 0.0);
  assembleThermalDiffusionContribution(
      mesh, k, temperature, makeFixedTemperatureBoundaries(mesh, tLeft, tRight, tTop, tBottom),
      genericBuilder, genericRhs);
  const auto genericMatrix = genericBuilder.build();
  ASSERT_EQ(matrix.nonZeros(), genericMatrix.nonZeros());
  for (Index k2 = 0; k2 < matrix.nonZeros(); ++k2) {
    EXPECT_EQ(matrix.columnIndicesData()[k2], genericMatrix.columnIndicesData()[k2]) << k2;
    EXPECT_EQ(matrix.valuesData()[k2], genericMatrix.valuesData()[k2]) << k2;
  }
  for (Index row = 0; row < n; ++row) EXPECT_EQ(rhs[row], genericRhs[row]) << row;
}

TEST(EnergyEquationThermalBCIntegrationTest, HeatFluxAndAdiabaticBoundariesMatchHandDerivedRhs) {
  // Single-cell mesh (1x1) so every one of the cell's 4 boundary faces
  // has known area=1.0/distance=0.5, and the whole diagonal/RHS is fully
  // hand-derivable: 3 Adiabatic faces (left/top/bottom) plus 1 HeatFlux
  // face (right), all sharing the same diffusion conductivity k=3.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const Real k = 3.0;
  const Real q = 6.0;                     // heat flux [W/m^2], leaving the domain.
  const Real heatFluxConductivity = 3.0;  // same material as the diffusion k above.
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "bottom", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "right", std::make_unique<HeatFlux>(q, heatFluxConductivity));

  const Index n = mesh.numberOfCells();
  ASSERT_EQ(n, 1u);
  const Real diffusionCoefficient = 6.0;  // k*A/d = 3*1.0/0.5, every boundary face here.
  // P12-MESH-003: a gradient-type face contributes its exact prescribed
  // heat flow -Df (T_b - T_P) = -Df g d to the RHS and nothing to the
  // diagonal (the pre-fix form, Df on the diagonal and Df T_b(T_P^old) on
  // the RHS, lagged the flux by one outer iteration). Independent of the
  // cell temperature, so checked at two of them.
  for (const Real t0 : {100.0, -50.0}) {
    const ScalarField temperature(n, t0);
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleThermalDiffusionContribution(mesh, k, temperature, boundaries, builder, rhs);
    const auto matrix = builder.build();
    EXPECT_EQ(matrix.nonZeros(), 0u);  // nothing depends on T: no coefficient at all
    // Adiabatic: g = 0 -> no heat through the 3 insulated faces.
    // HeatFlux: g = dT/dn = -q/k_bc = -6/3 = -2 -> Df * g * d = 6 * (-2 * 0.5)
    // = -6 = -q * A: the 6 W leaving through the right face.
    const Real heatFluxGradient = -q / heatFluxConductivity;
    EXPECT_NEAR(rhs[0], diffusionCoefficient * (heatFluxGradient * 0.5), 1e-12);
    EXPECT_NEAR(rhs[0], -q * 1.0, 1e-12);  // literal hand-derived total.
  }
}
