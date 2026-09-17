// P3-PHYS-003: field-based (per-cell k(T)/cp(T)) EnergyEquation overloads.
// Mirrors test_energy_equation.cpp's own hand-derived-coefficient style,
// but for the new conductivity-field/specificHeat-field overloads and the
// field-based assembleEnergyEquation combiner, plus the mandatory
// constant-property-equivalence proof (P3-PHYS-003 section 4/19).
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalProperties.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::assembleEnergyEquation;
using cfd::thermal::assembleThermalConvectionContribution;
using cfd::thermal::assembleThermalDiffusionContribution;
using cfd::thermal::EnergyAssembly;
using cfd::thermal::ThermalProperties;

namespace {

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

Index cellTouchingPatch(const Mesh& mesh, std::string_view patchName) {
  return mesh.face(mesh.boundaryPatch(patchName).faceIds().front()).owner();
}

}  // namespace

// ---------------------------------------------------------------------
// Diffusion: field-based conductivity, hand-derived.
// ---------------------------------------------------------------------
TEST(EnergyEquationVariablePropertiesTest, DiffusionUniformFieldMatchesScalarOverloadExactly) {
  // Constant-property equivalence (P3-PHYS-003 section 4): a uniform k
  // field must reproduce the scalar overload's coefficients, same
  // tolerance floor already established for MomentumEquation's own
  // uniform-field-vs-scalar proof (interpolateInternalFace roundoff, not
  // physics).
  const Mesh mesh = makeTwoCellMesh();
  const Real k = 3.0;
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 10.0, 20.0, 15.0, 5.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  SparseMatrixBuilder scalarBuilder(n, n);
  Vector scalarRhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, k, temperature, boundaries, scalarBuilder, scalarRhs);
  const auto scalarMatrix = scalarBuilder.build();

  const ScalarField kField(n, k);
  SparseMatrixBuilder fieldBuilder(n, n);
  Vector fieldRhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, kField, temperature, boundaries, fieldBuilder,
                                       fieldRhs);
  const auto fieldMatrix = fieldBuilder.build();

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(fieldMatrix.diagonal(row), scalarMatrix.diagonal(row), 1e-11) << "row " << row;
    EXPECT_NEAR(fieldRhs[row], scalarRhs[row], 1e-9) << "row " << row;
  }
}

TEST(EnergyEquationVariablePropertiesTest, DiffusionInternalFaceMatchesHandDerivedValue) {
  // cell0 k=3, cell1 k=5 -> arithmetic-mean face value (equal weights on
  // this uniform mesh) = 4.0. Internal conductance = kFace*A/d = 4*1/1=4.
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 10.0, 20.0, 15.0, 5.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  ScalarField kField(n);
  kField[0] = 3.0;
  kField[1] = 5.0;

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, kField, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Index cellA = mesh.face(internalFaceId(mesh)).owner();
  const Index cellB = *mesh.face(internalFaceId(mesh)).neighbor();
  const Real internalConductanceExpected = 4.0;  // (3+5)/2 * 1.0/1.0 -- UNCHANGED by DIFF-002

  // P12-DIFF-002 A5, entry 6 (acceptance_gate_A5.md, class M-A/2). The interpolated internal-face
  // value is still exactly 4.0, and is still asserted -- in isolation, below. What changed is that
  // each cell's x-wall reaches its far cell through the shared internal face, so each row's entry
  // at the other cell also carries that row's OWN one-sided far-cell coefficient, scaled by its own
  // owner conductivity. Those two coefficients differ here (k = 3 vs 5), which is exactly why the
  // two entries are no longer equal. Derived in a5/tools/derive_expected.py, block B1:
  //   cF(owner) = k_owner |S| h1 / (h2 (h2 - h1)) = k_owner / 3   (h1 = 0.5, h2 = 1.5, |S| = 1)
  //   A(1,0) = -(4 + 5/3) = -17/3      A(0,1) = -(4 + 3/3) = -5      difference = 2/3
  const Real cFa = 3.0 / 3.0;  // owner = cell 0, k = 3
  const Real cFb = 5.0 / 3.0;  // owner = cell 1, k = 5

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  // matrix.multiply(eX)[Y] reads A(Y, X): the row is Y, so the far-cell term is row Y's own.
  EXPECT_NEAR(-matrix.multiply(eA)[cellB], internalConductanceExpected + cFb, 1e-10);
  EXPECT_NEAR(-matrix.multiply(eB)[cellA], internalConductanceExpected + cFa, 1e-10);
  // The difference between the two entries is exactly the difference of the two one-sided far-cell
  // coefficients -- nothing else has become asymmetric.
  EXPECT_NEAR(matrix.multiply(eB)[cellA] - matrix.multiply(eA)[cellB], cFb - cFa, 1e-12);

  // The interpolated value and the equal-and-opposite property, isolated: with gradient-type
  // boundaries no wall is reconstructed, so no far-cell term exists and the internal face's
  // contribution stands alone.
  SparseMatrixBuilder isolated(n, n);
  Vector isolatedRhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, kField, temperature, makeZeroGradientBoundaries(mesh),
                                       isolated, isolatedRhs);
  const auto isolatedMatrix = isolated.build();
  EXPECT_NEAR(-isolatedMatrix.multiply(eA)[cellB], internalConductanceExpected, 1e-10);
  EXPECT_NEAR(-isolatedMatrix.multiply(eB)[cellA], internalConductanceExpected, 1e-10);
  EXPECT_DOUBLE_EQ(isolatedMatrix.multiply(eA)[cellB], isolatedMatrix.multiply(eB)[cellA]);
}

TEST(EnergyEquationVariablePropertiesTest, DiffusionBoundaryFaceUsesOwnerConductivityDirectly) {
  // cell0's own k drives every boundary face touching it -- no
  // interpolation partner exists at a boundary.
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 10.0, 20.0, 15.0, 5.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  ScalarField kField(n);
  kField[0] = 3.0;
  kField[1] = 100.0;  // must have zero effect on cell 0's own boundary faces.

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, kField, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Index leftCell = cellTouchingPatch(mesh, "left");
  ASSERT_EQ(leftCell, 0u);
  // P12-DIFF-002 A5, entry 7. Every wall face of cell 0 is still driven by cell 0's own k = 3, but
  // the left wall is now the second-order reconstruction while top/bottom fall back (the y axis is
  // one cell thick). Derived in a5/tools/derive_expected.py, block B2, with h = 1 and |S| = 1:
  //   left wall:        cP = k[0] * 3/h = 9        (two-point would be k[0]/h1 = 6)
  //   top/bottom walls: k[0]/h1 = 6 each, fallback
  //   internal face:    (3 + 100)/2 * 1/1 = 51.5
  const Real cP = 3.0 * 3.0 / 1.0;        // k[0] * 3/h, reconstructed left wall
  const Real fallback = 3.0 * 1.0 / 0.5;  // k[0]*A/h1 = 6, one-cell-thick y walls
  const Real internalConductance = 51.5;
  const Real expectedDiagonal = cP + 2.0 * fallback + internalConductance;  // 72.5
  EXPECT_NEAR(matrix.diagonal(leftCell), expectedDiagonal, 1e-9);

  // A5 strengthening: the property this test is named for -- that a boundary face uses the OWNER's
  // conductivity and nothing else -- is now asserted directly. Only the internal face may react to
  // the neighbour's k, so cell 0's wall contribution (diagonal minus the internal coefficient) must
  // be identical when k[1] changes by an order of magnitude.
  ScalarField louder(n);
  louder[0] = 3.0;
  louder[1] = 1000.0;
  SparseMatrixBuilder loudBuilder(n, n);
  Vector loudRhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, louder, temperature, boundaries, loudBuilder, loudRhs);
  const auto loudMatrix = loudBuilder.build();
  const Real loudInternal = (3.0 + 1000.0) / 2.0;  // 501.5
  EXPECT_NEAR(loudMatrix.diagonal(leftCell) - loudInternal,
              matrix.diagonal(leftCell) - internalConductance, 1e-9);
  EXPECT_NEAR(loudMatrix.diagonal(leftCell) - loudInternal, cP + 2.0 * fallback, 1e-9);
  // ... and the prescribed-value term on cell 0's own walls is likewise owner-driven.
  EXPECT_NEAR(loudRhs[leftCell], rhs[leftCell], 1e-9);
}

TEST(EnergyEquationVariablePropertiesTest, RejectsNonFiniteOrNonPositiveConductivityField) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  ScalarField zeroK(n, 1.0);
  zeroK[0] = 0.0;
  EXPECT_THROW(
      assembleThermalDiffusionContribution(mesh, zeroK, temperature, boundaries, builder, rhs),
      InvalidArgumentError);

  ScalarField nanK(n, 1.0);
  nanK[1] = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(
      assembleThermalDiffusionContribution(mesh, nanK, temperature, boundaries, builder, rhs),
      InvalidArgumentError);
}

TEST(EnergyEquationVariablePropertiesTest, DiffusionMismatchedConductivityFieldSizeThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  const ScalarField wrongSizeK(n + 1, 1.0);
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  EXPECT_THROW(
      assembleThermalDiffusionContribution(mesh, wrongSizeK, temperature, boundaries, builder, rhs),
      InvalidArgumentError);
}

// ---------------------------------------------------------------------
// Convection: field-based specificHeat, upwind-cell (not face-
// interpolated) convention.
// ---------------------------------------------------------------------
TEST(EnergyEquationVariablePropertiesTest, ConvectionUniformFieldMatchesScalarOverloadExactly) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const Real cp = 2.0;
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[internalFaceId(mesh)] = 4.0;

  SparseMatrixBuilder scalarBuilder(n, n);
  Vector scalarRhs(n, 0.0);
  assembleThermalConvectionContribution(mesh, cp, massFlux, temperature, boundaries, scalarBuilder,
                                        scalarRhs);
  const auto scalarMatrix = scalarBuilder.build();

  const ScalarField cpField(n, cp);
  SparseMatrixBuilder fieldBuilder(n, n);
  Vector fieldRhs(n, 0.0);
  assembleThermalConvectionContribution(mesh, cpField, massFlux, temperature, boundaries,
                                        fieldBuilder, fieldRhs);
  const auto fieldMatrix = fieldBuilder.build();

  // Probed via matrix-vector products rather than matrix.diagonal(row)
  // directly -- a convection-only assembly does not necessarily store an
  // explicit diagonal entry for every row (e.g. a row that is never the
  // upwind side of any face it touches gets no diagonal contribution at
  // all, same as the scalar-cp overload), so .diagonal() would throw for
  // such a row (SparseMatrix::diagonal requires a stored entry). Comparing
  // full columns via multiply() is exact either way.
  for (Index col = 0; col < n; ++col) {
    Vector e(n, 0.0);
    e[col] = 1.0;
    const Vector scalarColumn = scalarMatrix.multiply(e);
    const Vector fieldColumn = fieldMatrix.multiply(e);
    for (Index row = 0; row < n; ++row) {
      EXPECT_NEAR(fieldColumn[row], scalarColumn[row], 1e-11) << "col " << col << " row " << row;
    }
  }
  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(fieldRhs[row], scalarRhs[row], 1e-11) << "row " << row;
  }
}

TEST(EnergyEquationVariablePropertiesTest, ConvectionUsesUpwindCellsOwnSpecificHeat) {
  // F>=0 at the internal face: owner (cellA) is upwind -> cp[cellA] drives
  // both this face's rows, cp[cellB] must have zero effect.
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const Index faceId = internalFaceId(mesh);
  const Index cellA = mesh.face(faceId).owner();
  const Index cellB = *mesh.face(faceId).neighbor();
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[faceId] = 5.0;  // owner (cellA) upwind.

  ScalarField cpField(n);
  cpField[cellA] = 2.0;
  cpField[cellB] = 999.0;  // must be irrelevant -- not the upwind cell.

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalConvectionContribution(mesh, cpField, massFlux, temperature, boundaries, builder,
                                        rhs);
  const auto matrix = builder.build();

  const Real expected = 2.0 * 5.0;  // cp[cellA]*F
  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], expected, 1e-12);
  EXPECT_NEAR(matrix.multiply(eA)[cellB], -expected, 1e-12);  // equal-and-opposite.
}

TEST(EnergyEquationVariablePropertiesTest,
     ConvectionSwitchesToNeighborsSpecificHeatWhenItIsUpwind) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const Index faceId = internalFaceId(mesh);
  const Index cellA = mesh.face(faceId).owner();
  const Index cellB = *mesh.face(faceId).neighbor();
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[faceId] = -5.0;  // neighbor (cellB) upwind.

  ScalarField cpField(n);
  cpField[cellA] = 999.0;  // must be irrelevant now.
  cpField[cellB] = 3.0;

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalConvectionContribution(mesh, cpField, massFlux, temperature, boundaries, builder,
                                        rhs);
  const auto matrix = builder.build();

  const Real expected = 3.0 * -5.0;  // cp[cellB]*F, F<0.
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eB)[cellA], expected, 1e-12);
  EXPECT_NEAR(matrix.multiply(eB)[cellB], -expected, 1e-12);
}

TEST(EnergyEquationVariablePropertiesTest, RejectsNonFiniteOrNonPositiveSpecificHeatField) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  ScalarField zeroCp(n, 1.0);
  zeroCp[0] = 0.0;
  EXPECT_THROW(assembleThermalConvectionContribution(mesh, zeroCp, massFlux, temperature,
                                                     boundaries, builder, rhs),
               InvalidArgumentError);
}

// ---------------------------------------------------------------------
// Combined field-based assembleEnergyEquation.
// ---------------------------------------------------------------------
TEST(EnergyEquationVariablePropertiesTest, CombinerUniformFieldsMatchScalarThermalProperties) {
  const Mesh mesh = makeTwoCellMesh();
  const Real k = 3.0, cp = 2.0;
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 10.0, 20.0, 15.0, 5.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[internalFaceId(mesh)] = 4.0;
  const Real q = 6.0;

  const ThermalProperties thermal(k, cp);
  const EnergyAssembly scalarAssembly =
      assembleEnergyEquation(mesh, temperature, massFlux, thermal, boundaries, q);

  const ScalarField kField(n, k);
  const ScalarField cpField(n, cp);
  const EnergyAssembly fieldAssembly =
      assembleEnergyEquation(mesh, temperature, massFlux, kField, cpField, boundaries, q);

  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(fieldAssembly.system.rhs()[i], scalarAssembly.system.rhs()[i], 1e-9) << "row " << i;
    EXPECT_NEAR(fieldAssembly.diagonal[i], scalarAssembly.diagonal[i], 1e-9) << "row " << i;
  }
}

TEST(EnergyEquationVariablePropertiesTest, CombinerProducesCorrectDimensionsAndIsFinite) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 3, 2.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField kField(n, 0.6);
  const ScalarField cpField(n, 4180.0);

  const EnergyAssembly assembly =
      assembleEnergyEquation(mesh, temperature, massFlux, kField, cpField, boundaries);

  EXPECT_EQ(assembly.system.matrix().rows(), n);
  EXPECT_EQ(assembly.system.rhs().size(), n);
  EXPECT_TRUE(assembly.system.matrix().allFinite());
  EXPECT_TRUE(assembly.system.rhs().allFinite());
}

TEST(EnergyEquationVariablePropertiesTest, CombinerMismatchedFieldSizeThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField wrongSizeK(n + 1, 1.0);
  const ScalarField cpField(n, 1.0);

  EXPECT_THROW(
      (void)assembleEnergyEquation(mesh, temperature, massFlux, wrongSizeK, cpField, boundaries),
      InvalidArgumentError);
}

TEST(EnergyEquationVariablePropertiesTest, CombinerRepeatedAssemblyIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 350.0, 300.0, 320.0, 320.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 310.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.7;
  }
  ScalarField kField(n);
  ScalarField cpField(n);
  for (Index i = 0; i < n; ++i) {
    kField[i] = 0.5 + 0.01 * static_cast<Real>(i);  // genuinely non-uniform.
    cpField[i] = 4000.0 + static_cast<Real>(i);
  }

  const EnergyAssembly a =
      assembleEnergyEquation(mesh, temperature, massFlux, kField, cpField, boundaries, 2.0);
  const EnergyAssembly b =
      assembleEnergyEquation(mesh, temperature, massFlux, kField, cpField, boundaries, 2.0);

  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.system.rhs()[i], b.system.rhs()[i]);
    EXPECT_EQ(a.diagonal[i], b.diagonal[i]);
  }
}
