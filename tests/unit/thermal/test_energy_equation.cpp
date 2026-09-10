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
  // k=3: boundary conductance = k*A/d = 3*1.0/0.5 = 6 per boundary face
  // (3 per cell: one left-or-right, one top, one bottom); internal
  // conductance = k*A/d = 3*1.0/1.0 = 3. FixedValue ignores the owner
  // field value, so the temperature field's own contents are irrelevant.
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

  const Real boundaryConductance = 6.0;  // k*A/d = 3*1.0/0.5
  const Real internalConductance = 3.0;  // k*A/d = 3*1.0/1.0
  const Real expectedDiagonal = 3.0 * boundaryConductance + internalConductance;  // 21

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellB], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -internalConductance, 1e-10);
  EXPECT_NEAR(matrix.multiply(eA)[cellB], -internalConductance, 1e-10);

  const Real commonRhs = boundaryConductance * tTop + boundaryConductance * tBottom;
  const Real expectedRhsLeft = boundaryConductance * tLeft + commonRhs;
  const Real expectedRhsRight = boundaryConductance * tRight + commonRhs;
  EXPECT_NEAR(rhs[leftCell], expectedRhsLeft, 1e-9);
  EXPECT_NEAR(rhs[rightCell], expectedRhsRight, 1e-9);
}

// ---------------------------------------------------------------------
// B. Diffusion symmetry (conservative equal/opposite internal coupling).
// ---------------------------------------------------------------------
TEST(EnergyEquationDiffusionTest, InternalFaceCoefficientsAreSymmetric) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto boundaries = makeFixedTemperatureBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, 1.0, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  Vector e0(n, 0.0);
  e0[0] = 1.0;
  Vector e1(n, 0.0);
  e1[1] = 1.0;
  const Real a10 = matrix.multiply(e0)[1];  // A(1,0)
  const Real a01 = matrix.multiply(e1)[0];  // A(0,1)
  EXPECT_NEAR(a01, a10, 1e-12);
  EXPECT_LT(a01, 0.0);
  EXPECT_TRUE(matrix.allFinite());
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

  const Real boundaryConductance = 6.0;                                       // k*A/d = 3*1.0/0.5
  const Real internalConductance = 3.0;                                       // k*A/d = 3*1.0/1.0
  const Real convection = cp * fluxAtoB;                                      // 8.0
  const Real diagonalBase = 3.0 * boundaryConductance + internalConductance;  // 21

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], diagonalBase + convection, 1e-9);  // cellA upwind
  EXPECT_NEAR(matrix.multiply(eB)[cellB], diagonalBase, 1e-9);               // cellB not upwind
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -internalConductance, 1e-9);       // diffusion only
  EXPECT_NEAR(matrix.multiply(eA)[cellB], -internalConductance - convection, 1e-9);

  const Index leftCell = cellTouchingPatch(mesh, "left");
  const Index rightCell = cellTouchingPatch(mesh, "right");
  const Real commonRhs = boundaryConductance * tTop + boundaryConductance * tBottom;
  const Real volume = mesh.cell(0).volume();  // both cells are unit squares here.
  EXPECT_NEAR(rhs[leftCell], boundaryConductance * tLeft + commonRhs + q * volume, 1e-9);
  EXPECT_NEAR(rhs[rightCell], boundaryConductance * tRight + commonRhs + q * volume, 1e-9);

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
  // (unlike FixedValue, which ignores it) -- a non-finite owner value
  // therefore propagates into the assembled RHS.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  ScalarField temperature(mesh.numberOfCells(), 300.0);
  temperature[0] = std::numeric_limits<Real>::quiet_NaN();
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
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

  const Real boundaryConductance = 6.0;
  const Real internalConductance = 3.0;
  const Real expectedDiagonal = 3.0 * boundaryConductance + internalConductance;  // 21

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellB], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -internalConductance, 1e-10);

  const Real commonRhs = boundaryConductance * tTop + boundaryConductance * tBottom;
  EXPECT_NEAR(rhs[leftCell], boundaryConductance * tLeft + commonRhs, 1e-9);
  EXPECT_NEAR(rhs[rightCell], boundaryConductance * tRight + commonRhs, 1e-9);
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
  const Real t0 = 100.0;
  const ScalarField temperature(n, t0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, k, temperature, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Real diffusionCoefficient = 6.0;  // k*A/d = 3*1.0/0.5, every boundary face here.
  EXPECT_NEAR(matrix.diagonal(0), 4.0 * diffusionCoefficient, 1e-10);  // 24

  // Adiabatic's boundaryValue is the owner value itself (zero-gradient),
  // so each of the 3 Adiabatic faces contributes diffusionCoefficient*t0.
  const Real adiabaticContribution = 3.0 * diffusionCoefficient * t0;  // 1800
  // HeatFlux: dT/dn = -q/k_bc = -6/3 = -2 -> tB = t0 + dT/dn*distance =
  // 100 + (-2*0.5) = 99 -> contribution = diffusionCoefficient*tB = 594.
  const Real heatFluxGradient = -q / heatFluxConductivity;
  const Real heatFluxBoundaryValue = t0 + (heatFluxGradient * 0.5);
  const Real heatFluxContribution = diffusionCoefficient * heatFluxBoundaryValue;
  EXPECT_NEAR(rhs[0], adiabaticContribution + heatFluxContribution, 1e-9);
  EXPECT_NEAR(rhs[0], 2394.0, 1e-9);  // literal hand-derived total.
}
