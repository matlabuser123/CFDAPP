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
  const Real internalConductanceExpected = 4.0;  // (3+5)/2 * 1.0/1.0

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(-matrix.multiply(eA)[cellB], internalConductanceExpected, 1e-10);
  EXPECT_NEAR(-matrix.multiply(eB)[cellA], internalConductanceExpected, 1e-10);
  // Equal-and-opposite conservation.
  EXPECT_NEAR(matrix.multiply(eA)[cellB], matrix.multiply(eB)[cellA], 1e-12);
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
  const Real boundaryConductanceExpected = 3.0 * 1.0 / 0.5;  // k[0]*A/d = 6.
  // leftCell's diagonal = 3 boundary faces (left/top/bottom, all using
  // k[0]=3) + 1 internal face ((3+100)/2 * 1/1 = 51.5).
  const Real expectedDiagonal = 3.0 * boundaryConductanceExpected + 51.5;
  EXPECT_NEAR(matrix.diagonal(leftCell), expectedDiagonal, 1e-9);
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
