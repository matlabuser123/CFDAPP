// P3-PHYS-004: SpeciesEquation -- contribution-level hand-derived tests,
// mirroring test_energy_equation.cpp's own structure (this task's own
// section 2: energy equation is the closest architectural analogue).
// Also covers species boundary-condition integration (section 13: no new
// BC classes -- the generic FixedValue/FixedGradient are used directly)
// and the diffusivity-model role (section 12) at the combiner level,
// consolidating what the task's own suggested file list splits into
// separate test_species_boundary_conditions.cpp/test_species_diffusivity.cpp
// -- a deliberate, disclosed consolidation (this task's own section 32:
// "Use existing test organization if it has a better convention").
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/species/SpeciesEquation.hpp"
#include "cfd/species/SpeciesProperties.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::species::assembleSpeciesDiffusionContribution;
using cfd::species::assembleSpeciesSourceContribution;
using cfd::species::assembleSpeciesTransportEquation;
using cfd::species::ConcentrationBounds;
using cfd::species::concentrationBounds;
using cfd::species::SpeciesAssembly;
using cfd::species::SpeciesProperties;

// assembleSpeciesConvectionContribution isn't ADL-visible without the
// species:: prefix in a few call sites below -- imported alongside the
// rest above via the using-declaration, matching test_energy_equation.cpp's
// own style.
using cfd::species::assembleSpeciesConvectionContribution;

namespace {

// Same 2-cell channel convention as test_energy_equation.cpp's own
// makeTwoCellMesh(): dx=dy=1.0, boundary faces area=1.0/distance=0.5
// (conductance 2*coeff), internal face area=1.0/distance=1.0 (conductance
// coeff).
Mesh makeTwoCellMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

BoundaryConditionSet makeFixedConcentrationBoundaries(const Mesh& mesh, Real left, Real right,
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
// A. Two-cell diffusion system (hand-derived).
// ---------------------------------------------------------------------
TEST(SpeciesEquationDiffusionTest, TwoCellSystemMatchesHandDerivedCoefficients) {
  const Mesh mesh = makeTwoCellMesh();
  const Real diffusionCoefficient = 3.0;  // rho*D, already resolved.
  const Real yLeft = 1.0, yRight = 0.0, yTop = 0.5, yBottom = 0.5;
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, yLeft, yRight, yTop, yBottom);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesDiffusionContribution(mesh, diffusionCoefficient, concentration, boundaries,
                                       builder, rhs);
  const auto matrix = builder.build();

  const Index cellA = mesh.face(internalFaceId(mesh)).owner();
  const Index cellB = *mesh.face(internalFaceId(mesh)).neighbor();
  const Index leftCell = cellTouchingPatch(mesh, "left");
  const Index rightCell = cellTouchingPatch(mesh, "right");

  const Real boundaryConductance = 6.0;  // coeff*A/d = 3*1.0/0.5
  const Real internalConductance = 3.0;  // coeff*A/d = 3*1.0/1.0
  const Real expectedDiagonal = 3.0 * boundaryConductance + internalConductance;  // 21

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellB], expectedDiagonal, 1e-10);
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -internalConductance, 1e-10);
  EXPECT_NEAR(matrix.multiply(eA)[cellB], -internalConductance, 1e-10);

  const Real commonRhs = boundaryConductance * yTop + boundaryConductance * yBottom;
  EXPECT_NEAR(rhs[leftCell], boundaryConductance * yLeft + commonRhs, 1e-9);
  EXPECT_NEAR(rhs[rightCell], boundaryConductance * yRight + commonRhs, 1e-9);
}

TEST(SpeciesEquationDiffusionTest, InternalFaceCoefficientsAreSymmetric) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesDiffusionContribution(mesh, 1.0, concentration, boundaries, builder, rhs);
  const auto matrix = builder.build();

  Vector e0(n, 0.0);
  e0[0] = 1.0;
  Vector e1(n, 0.0);
  e1[1] = 1.0;
  const Real a10 = matrix.multiply(e0)[1];
  const Real a01 = matrix.multiply(e1)[0];
  EXPECT_NEAR(a01, a10, 1e-12);
  EXPECT_LT(a01, 0.0);
  EXPECT_TRUE(matrix.allFinite());
}

TEST(SpeciesEquationDiffusionTest, ConstantConcentrationGivesZeroContributionEverywhere) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real value = 0.7;
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, value, value, value, value);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, value);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesDiffusionContribution(mesh, 2.0, concentration, boundaries, builder, rhs);
  const auto matrix = builder.build();

  const Vector yExact(n, value);
  const Vector residual = matrix.multiply(yExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-10);
  }
}

TEST(SpeciesEquationDiffusionTest, ZeroDiffusionCoefficientGivesZeroDiffusiveContribution) {
  // Section 12: D=0 (hence diffusionCoefficient=rho*0=0) must be
  // accepted, not rejected -- pure-advection support.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, 1.0, 0.0, 0.5, 0.5);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesDiffusionContribution(mesh, 0.0, concentration, boundaries, builder, rhs);
  const auto matrix = builder.build();

  // Probed via matrix-vector products rather than matrix.diagonal(row) --
  // a zero-coefficient diffusion assembly need not store an explicit
  // (structural) diagonal entry for every row, so .diagonal() can throw
  // (SparseMatrix::diagonal requires a stored entry); multiply() is exact
  // either way and confirms the contribution is genuinely zero, not just
  // unset.
  for (Index col = 0; col < n; ++col) {
    Vector e(n, 0.0);
    e[col] = 1.0;
    const Vector column = matrix.multiply(e);
    for (Index row = 0; row < n; ++row) {
      EXPECT_DOUBLE_EQ(column[row], 0.0) << "col " << col << " row " << row;
    }
    EXPECT_DOUBLE_EQ(rhs[col], 0.0);
  }
}

TEST(SpeciesEquationDiffusionTest, MismatchedConcentrationSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, 0.0, 0.0, 0.0, 0.0);
  const ScalarField concentration(mesh.numberOfCells() + 1, 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(
      assembleSpeciesDiffusionContribution(mesh, 1.0, concentration, boundaries, builder, rhs),
      InvalidArgumentError);
}

// ---------------------------------------------------------------------
// B. Upwind convection direction (mirrors EnergyEquationConvectionTest,
//    coefficient 1 instead of specificHeat*flux -- Y is the transported
//    quantity itself).
// ---------------------------------------------------------------------
TEST(SpeciesEquationConvectionTest, PositiveInternalFluxSelectsOwnerNegativeSelectsNeighbor) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.0);
  const Index faceId = internalFaceId(mesh);
  const Index owner = mesh.face(faceId).owner();
  const Index neighbor = *mesh.face(faceId).neighbor();

  {
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    massFlux[faceId] = 5.0;
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleSpeciesConvectionContribution(mesh, massFlux, concentration, boundaries, builder, rhs);
    const auto matrix = builder.build();
    Vector eOwner(n, 0.0);
    eOwner[owner] = 1.0;
    EXPECT_NEAR(matrix.multiply(eOwner)[owner], 5.0, 1e-12);
    EXPECT_NEAR(matrix.multiply(eOwner)[neighbor], -5.0, 1e-12);
  }
  {
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    massFlux[faceId] = -5.0;
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleSpeciesConvectionContribution(mesh, massFlux, concentration, boundaries, builder, rhs);
    const auto matrix = builder.build();
    Vector eNeighbor(n, 0.0);
    eNeighbor[neighbor] = 1.0;
    EXPECT_NEAR(matrix.multiply(eNeighbor)[owner], -5.0, 1e-12);
    EXPECT_NEAR(matrix.multiply(eNeighbor)[neighbor], 5.0, 1e-12);
  }
}

TEST(SpeciesEquationConvectionTest, BoundaryOutflowUsesOwnerUnknownNotBoundaryValue) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, 1.0, 0.0, 0.5, 0.5);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.0);

  const Index rightFaceId = mesh.boundaryPatch("right").faceIds().front();
  const Index ownerCell = mesh.face(rightFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[rightFaceId] = 4.0;  // outflow

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesConvectionContribution(mesh, massFlux, concentration, boundaries, builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], 4.0, 1e-12);
  EXPECT_NEAR(rhs[ownerCell], 0.0, 1e-12);
}

TEST(SpeciesEquationConvectionTest, BoundaryInflowUsesBoundaryValueOnRhs) {
  const Mesh mesh = makeTwoCellMesh();
  const Real yLeft = 1.0;
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, yLeft, 0.0, 0.5, 0.5);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.0);

  const Index leftFaceId = mesh.boundaryPatch("left").faceIds().front();
  const Index ownerCell = mesh.face(leftFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[leftFaceId] = -3.0;  // inflow: species entering at the known inlet value.

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesConvectionContribution(mesh, massFlux, concentration, boundaries, builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], 0.0, 1e-12);
  EXPECT_NEAR(rhs[ownerCell], 3.0 * yLeft, 1e-12);  // -(-3)*yLeft.
}

TEST(SpeciesEquationConvectionTest,
     ConstantConcentrationWithDivergenceFreeFluxGivesZeroNetContribution) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real value = 0.3;
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, value, value, value, value);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, value);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesConvectionContribution(mesh, massFlux, concentration, boundaries, builder, rhs);
  const auto matrix = builder.build();
  const Vector yExact(n, value);
  const Vector residual = matrix.multiply(yExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-12);
  }
}

// ---------------------------------------------------------------------
// C. Source RHS.
// ---------------------------------------------------------------------
TEST(SpeciesEquationSourceTest, AddsSTimesVolumeExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 2.0, 2.0);
  const Index n = mesh.numberOfCells();
  Vector rhs(n, 0.0);
  const Real s = 0.01;
  assembleSpeciesSourceContribution(mesh, s, rhs);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(rhs[cell.id()], s * cell.volume(), 1e-12);
  }
}

TEST(SpeciesEquationSourceTest, RejectsNonFiniteSource) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  Vector rhs(mesh.numberOfCells(), 0.0);
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(assembleSpeciesSourceContribution(mesh, nan, rhs), InvalidArgumentError);
}

// ---------------------------------------------------------------------
// D. Combined assembly, and the diffusivity-model role (section 12): rho
//    from FluidProperties and D from SpeciesProperties are multiplied
//    together into exactly the diffusionCoefficient used above.
// ---------------------------------------------------------------------
TEST(SpeciesEquationAssemblyTest, CombinedAssemblyUsesDensityTimesDiffusivityAsCoefficient) {
  const Mesh mesh = makeTwoCellMesh();
  const Real density = 2.0;
  const Real diffusivity = 1.5;  // diffusionCoefficient = rho*D = 3.0, same numbers as section A.
  const FluidProperties fluid(density, /*dynamicViscosity=*/1.0);
  const SpeciesProperties species("tracer", diffusivity);
  const Real yLeft = 10.0, yRight = 20.0, yTop = 15.0, yBottom = 5.0;
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, yLeft, yRight, yTop, yBottom);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.0);

  const Index faceId = internalFaceId(mesh);
  const Index cellA = mesh.face(faceId).owner();
  const Index cellB = *mesh.face(faceId).neighbor();
  const Real fluxAtoB = 4.0;
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[faceId] = fluxAtoB;

  const SpeciesAssembly assembly =
      assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species, boundaries);
  const auto& matrix = assembly.system.matrix();
  const auto& rhs = assembly.system.rhs();

  const Real boundaryConductance = 6.0;  // (rho*D)*A/d = 3*1.0/0.5
  const Real internalConductance = 3.0;  // (rho*D)*A/d = 3*1.0/1.0
  const Real diagonalBase = 3.0 * boundaryConductance + internalConductance;  // 21

  Vector eA(n, 0.0);
  eA[cellA] = 1.0;
  Vector eB(n, 0.0);
  eB[cellB] = 1.0;
  EXPECT_NEAR(matrix.multiply(eA)[cellA], diagonalBase + fluxAtoB, 1e-9);
  EXPECT_NEAR(matrix.multiply(eB)[cellB], diagonalBase, 1e-9);
  EXPECT_NEAR(matrix.multiply(eB)[cellA], -internalConductance, 1e-9);
  EXPECT_NEAR(matrix.multiply(eA)[cellB], -internalConductance - fluxAtoB, 1e-9);

  const Index leftCell = cellTouchingPatch(mesh, "left");
  const Real commonRhs = boundaryConductance * yTop + boundaryConductance * yBottom;
  EXPECT_NEAR(rhs[leftCell], boundaryConductance * yLeft + commonRhs, 1e-9);

  EXPECT_TRUE(matrix.allFinite());
  EXPECT_TRUE(rhs.allFinite());
}

TEST(SpeciesEquationAssemblyTest, ProducesCorrectDimensions) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 3, 2.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-5);

  const SpeciesAssembly assembly =
      assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species, boundaries);

  EXPECT_EQ(assembly.system.matrix().rows(), n);
  EXPECT_EQ(assembly.system.matrix().columns(), n);
  EXPECT_EQ(assembly.system.rhs().size(), n);
  EXPECT_EQ(assembly.diagonal.size(), n);
}

TEST(SpeciesEquationAssemblyTest, MismatchedConcentrationSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField concentration(mesh.numberOfCells() + 1, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-5);

  EXPECT_THROW((void)assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species,
                                                      boundaries),
               InvalidArgumentError);
}

TEST(SpeciesEquationAssemblyTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField concentration(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-5);

  EXPECT_THROW((void)assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species,
                                                      boundaries),
               InvalidArgumentError);
}

TEST(SpeciesEquationAssemblyTest, NonFiniteConcentrationAtGradientBoundaryThrowsNumericalError) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  ScalarField concentration(mesh.numberOfCells(), 0.3);
  concentration[0] = std::numeric_limits<Real>::quiet_NaN();
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-5);

  EXPECT_THROW((void)assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species,
                                                      boundaries),
               NumericalError);
}

TEST(SpeciesEquationAssemblyTest, RepeatedAssemblyIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const auto boundaries = makeFixedConcentrationBoundaries(mesh, 1.0, 0.0, 0.5, 0.5);
  const Index n = mesh.numberOfCells();
  ScalarField concentration(n, 0.3);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.4;
  }
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-5);

  const SpeciesAssembly a =
      assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species, boundaries);
  const SpeciesAssembly b =
      assembleSpeciesTransportEquation(mesh, concentration, massFlux, fluid, species, boundaries);

  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.system.rhs()[i], b.system.rhs()[i]);
    EXPECT_EQ(a.diagonal[i], b.diagonal[i]);
  }
}

// ---------------------------------------------------------------------
// E. Species boundary conditions (section 13): generic FixedValue/
//    FixedGradient reused directly, no species-specific BC classes.
// ---------------------------------------------------------------------
TEST(SpeciesBoundaryConditionTest, FixedValueActsAsDirichletInflowSource) {
  // Already proven numerically above
  // (BoundaryInflowUsesBoundaryValueOnRhs/TwoCellSystemMatchesHandDerivedCoefficients)
  // -- this test exists to name the section-13 requirement explicitly:
  // FixedValue alone is sufficient for a prescribed-inlet-concentration
  // boundary, no new class needed.
  const Mesh mesh = makeTwoCellMesh();
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(1.0));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  EXPECT_TRUE(boundaries.has("left"));
  EXPECT_TRUE(boundaries.has("right"));
}

TEST(SpeciesBoundaryConditionTest, ZeroGradientGivesNoDiffusiveFluxThroughAnImpermeableWall) {
  // Section 15: default impermeable-wall diffusive flux is zero
  // (dY/dn=0) -- FixedGradient(0.0) directly expresses this, and its
  // contribution to the RHS must be exactly conductance*ownerValue (a
  // pure identity, adding nothing net once folded into A*Y=b for a
  // uniform field -- proven generally by
  // ConstantConcentrationGivesZeroContributionEverywhere above; this
  // test isolates the boundary term itself).
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  const Index n = mesh.numberOfCells();
  const ScalarField concentration(n, 0.42);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleSpeciesDiffusionContribution(mesh, 1.0, concentration, boundaries, builder, rhs);
  const auto matrix = builder.build();

  // FixedGradient(0.0)'s boundaryValue == ownerValue, so A*Y - b == 0 for
  // this uniform field -- no net diffusive species enters or leaves
  // through any of the 4 zero-gradient walls.
  const Vector yExact(n, 0.42);
  const Vector residual = matrix.multiply(yExact) - rhs;
  EXPECT_NEAR(residual[0], 0.0, 1e-12);
}

// ---------------------------------------------------------------------
// F. Boundedness diagnostic (section 17).
// ---------------------------------------------------------------------
TEST(ConcentrationBoundsTest, ReportsMinAndMax) {
  ScalarField concentration(5);
  concentration[0] = 0.1;
  concentration[1] = 0.9;
  concentration[2] = 0.5;
  concentration[3] = -0.02;  // deliberately out of [0,1] -- reported, not clipped.
  concentration[4] = 1.05;
  const ConcentrationBounds bounds = concentrationBounds(concentration);
  EXPECT_DOUBLE_EQ(bounds.minimum, -0.02);
  EXPECT_DOUBLE_EQ(bounds.maximum, 1.05);
}

TEST(ConcentrationBoundsTest, DoesNotModifyTheField) {
  ScalarField concentration(3);
  concentration[0] = -0.02;
  concentration[1] = 0.5;
  concentration[2] = 1.05;
  (void)concentrationBounds(concentration);
  EXPECT_DOUBLE_EQ(concentration[0], -0.02);
  EXPECT_DOUBLE_EQ(concentration[2], 1.05);
}
