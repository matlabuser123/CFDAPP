// P3-PHYS-005: VolumeFractionEquation -- contribution-level hand-derived
// tests (section 24), the zero-velocity-preservation identity at the
// assembly level (section 26), plus the boundedness/phase-volume
// diagnostics (sections 14-15, 32).
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
#include "cfd/multiphase/VolumeFractionEquation.hpp"

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
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::multiphase::AlphaBounds;
using cfd::multiphase::assembleVolumeFractionConvectionContribution;
using cfd::multiphase::assembleVolumeFractionTransportEquation;
using cfd::multiphase::phaseVolume;
using cfd::multiphase::VolumeFractionAssembly;
using cfd::multiphase::volumeFractionBounds;

namespace {

Mesh makeTwoCellMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

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

}  // namespace

// ---------------------------------------------------------------------
// A. Convection contribution (mirrors SpeciesEquationConvectionTest).
// ---------------------------------------------------------------------
TEST(VolumeFractionConvectionTest, PositiveInternalFluxSelectsOwnerNegativeSelectsNeighbor) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField alpha(n, 0.0);
  const Index faceId = internalFaceId(mesh);
  const Index owner = mesh.face(faceId).owner();
  const Index neighbor = *mesh.face(faceId).neighbor();

  {
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    massFlux[faceId] = 5.0;
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleVolumeFractionConvectionContribution(mesh, massFlux, alpha, boundaries, builder, rhs);
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
    assembleVolumeFractionConvectionContribution(mesh, massFlux, alpha, boundaries, builder, rhs);
    const auto matrix = builder.build();
    Vector eNeighbor(n, 0.0);
    eNeighbor[neighbor] = 1.0;
    EXPECT_NEAR(matrix.multiply(eNeighbor)[owner], -5.0, 1e-12);
    EXPECT_NEAR(matrix.multiply(eNeighbor)[neighbor], 5.0, 1e-12);
  }
}

TEST(VolumeFractionConvectionTest, BoundaryOutflowUsesOwnerUnknownNotBoundaryValue) {
  const Mesh mesh = makeTwoCellMesh();
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(1.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  const Index n = mesh.numberOfCells();
  const ScalarField alpha(n, 0.0);
  const Index rightFaceId = mesh.boundaryPatch("right").faceIds().front();
  const Index ownerCell = mesh.face(rightFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[rightFaceId] = 4.0;

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleVolumeFractionConvectionContribution(mesh, massFlux, alpha, boundaries, builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], 4.0, 1e-12);
  EXPECT_NEAR(rhs[ownerCell], 0.0, 1e-12);
}

TEST(VolumeFractionConvectionTest, BoundaryInflowUsesBoundaryValueOnRhs) {
  const Mesh mesh = makeTwoCellMesh();
  const Real alphaLeft = 1.0;
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(alphaLeft));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  const Index n = mesh.numberOfCells();
  const ScalarField alpha(n, 0.0);
  const Index leftFaceId = mesh.boundaryPatch("left").faceIds().front();
  const Index ownerCell = mesh.face(leftFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[leftFaceId] = -3.0;

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleVolumeFractionConvectionContribution(mesh, massFlux, alpha, boundaries, builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], 0.0, 1e-12);
  EXPECT_NEAR(rhs[ownerCell], 3.0 * alphaLeft, 1e-12);
}

TEST(VolumeFractionConvectionTest, ConstantAlphaWithDivergenceFreeFluxGivesZeroNetContribution) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real value = 0.4;
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedValue>(value));
  }
  const Index n = mesh.numberOfCells();
  const ScalarField alpha(n, value);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleVolumeFractionConvectionContribution(mesh, massFlux, alpha, boundaries, builder, rhs);
  const auto matrix = builder.build();
  const Vector alphaExact(n, value);
  const Vector residual = matrix.multiply(alphaExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-12);
  }
}

TEST(VolumeFractionConvectionTest, MismatchedAlphaSizeThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alpha(mesh.numberOfCells() + 1, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(
      assembleVolumeFractionConvectionContribution(mesh, massFlux, alpha, boundaries, builder, rhs),
      InvalidArgumentError);
}

TEST(VolumeFractionConvectionTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alpha(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(
      assembleVolumeFractionConvectionContribution(mesh, massFlux, alpha, boundaries, builder, rhs),
      InvalidArgumentError);
}

// ---------------------------------------------------------------------
// B. Transient combiner.
// ---------------------------------------------------------------------
TEST(VolumeFractionTransportEquationTest, ZeroVelocityPreservesAlphaExactly) {
  // Section 26's own mandatory regression, checked here directly at the
  // assembly level: with u=v=0 everywhere (massFlux==0) the time-
  // derivative term is the *only* nonzero contribution, giving
  // (V/dt)*alphaNew = (V/dt)*alphaOld row-by-row -- i.e. alphaOld itself
  // is the exact solution of the assembled system, for *any* (non-
  // uniform) alpha field, not just a constant one.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  ScalarField alphaOld(n);
  for (Index i = 0; i < n; ++i) alphaOld[i] = 0.1 + 0.05 * static_cast<Real>(i % 4);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const Real dt = 0.01;

  const VolumeFractionAssembly assembly =
      assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, boundaries, dt);
  Vector alphaOldVec(n);
  for (Index i = 0; i < n; ++i) alphaOldVec[i] = alphaOld[i];
  const Vector residual = assembly.system.matrix().multiply(alphaOldVec) - assembly.system.rhs();
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-10) << "cell " << i;
  }
}

TEST(VolumeFractionTransportEquationTest, TimeDerivativeCoefficientMatchesVolumeOverDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 2.0, 2.0);  // cell volume = (2/3)^2.
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField alphaOld(n, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const Real dt = 0.1;

  const VolumeFractionAssembly assembly =
      assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, boundaries, dt);
  const Real expectedDiagonal = mesh.cell(0).volume() / dt;
  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(assembly.diagonal[row], expectedDiagonal, 1e-9) << "row " << row;
  }
}

TEST(VolumeFractionTransportEquationTest, MismatchedAlphaOldSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alphaOld(mesh.numberOfCells() + 1, 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW(
      (void)assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, boundaries, 0.1),
      InvalidArgumentError);
}

TEST(VolumeFractionTransportEquationTest, RejectsNonPositiveOrNonFiniteDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alphaOld(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW(
      (void)assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, boundaries, 0.0),
      InvalidArgumentError);
  EXPECT_THROW(
      (void)assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, boundaries, -0.1),
      InvalidArgumentError);
  EXPECT_THROW((void)assembleVolumeFractionTransportEquation(
                   mesh, alphaOld, massFlux, boundaries, std::numeric_limits<Real>::quiet_NaN()),
               InvalidArgumentError);
}

TEST(VolumeFractionTransportEquationTest, RepeatedAssemblyIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  ScalarField alphaOld(n, 0.3);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 0.4;
  }
  const VolumeFractionAssembly a =
      assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, boundaries, 0.05);
  const VolumeFractionAssembly b =
      assembleVolumeFractionTransportEquation(mesh, alphaOld, massFlux, boundaries, 0.05);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.system.rhs()[i], b.system.rhs()[i]);
    EXPECT_EQ(a.diagonal[i], b.diagonal[i]);
  }
}

// ---------------------------------------------------------------------
// C. Diagnostics.
// ---------------------------------------------------------------------
TEST(AlphaBoundsTest, ReportsMinAndMax) {
  ScalarField alpha(4);
  alpha[0] = -0.01;
  alpha[1] = 0.4;
  alpha[2] = 0.9;
  alpha[3] = 1.02;
  const AlphaBounds bounds = volumeFractionBounds(alpha);
  EXPECT_DOUBLE_EQ(bounds.minimum, -0.01);
  EXPECT_DOUBLE_EQ(bounds.maximum, 1.02);
}

TEST(PhaseVolumeTest, MatchesHandDerivedSum) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 2.0, 2.0);  // 4 cells, volume 1 each.
  ScalarField alpha(mesh.numberOfCells());
  alpha[0] = 1.0;
  alpha[1] = 0.5;
  alpha[2] = 0.0;
  alpha[3] = 0.25;
  EXPECT_NEAR(phaseVolume(mesh, alpha), 1.0 + 0.5 + 0.0 + 0.25, 1e-12);
}

TEST(PhaseVolumeTest, UniformAlphaGivesAlphaTimesDomainVolume) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 2.0, 3.0);
  const Real alphaValue = 0.3;
  const ScalarField alpha(mesh.numberOfCells(), alphaValue);
  Real domainVolume = 0.0;
  for (const auto& cell : mesh.cells()) domainVolume += cell.volume();
  EXPECT_NEAR(phaseVolume(mesh, alpha), alphaValue * domainVolume, 1e-9);
}

TEST(PhaseVolumeTest, MismatchedAlphaSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField alpha(mesh.numberOfCells() + 1, 0.5);
  EXPECT_THROW((void)phaseVolume(mesh, alpha), InvalidArgumentError);
}
