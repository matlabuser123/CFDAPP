#include <gtest/gtest.h>

#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::evaluateContinuity;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::correctFaceMassFlux;
using cfd::pressure_velocity::PressureCorrectionAssembly;

namespace {

Mesh makeTwoCellMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// Two-cell channel (left = cell 0, right = cell 1) with a Neumann
// ("inlet") left boundary and a Dirichlet, fixed-pressure ("outlet")
// right boundary -- the open-domain case this session added support for.
BoundaryConditionSet makeOpenChannelPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

}  // namespace

TEST(PressureCorrectionTest, TwoCellProbeSourceSignAndSolutionMatchHandDerivation) {
  // TODO.md section 22: the exact worked example -- left boundary
  // inflow=-1, internal face=+0.6, right boundary=+1. By hand (see the
  // reasoning in this session): with rho=1, d=1 uniform, uniform 1x1
  // cells (Af=1, dPN=1) so D_f=1, and reference cell = 0:
  //   cellImbalance = [-0.4, +0.4]  (predictor, before correction)
  //   rhs (pre-reference)  = [0.4, -0.4]
  //   rhs (after reference row 0 forced to p'=0) = [0, -0.4]
  //   solving: A(1,1)*p1' + A(1,0)*p0' = -0.4, A(1,1)=1, A(1,0)=-1, p0'=0
  //     -> p1' = -0.4
  //   F_internal' = D_f*(p0'-p1') = 1*(0-(-0.4)) = 0.4
  //   corrected internal flux = 0.6 + 0.4 = 1.0
  //   corrected imbalance: cell0 = -1+1.0=0, cell1 = -1.0+1.0=0 (exact)
  const Mesh mesh = makeTwoCellMesh();
  Index internalFaceId = 0, leftFaceId = 0, rightFaceId = 0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      internalFaceId = face.id();
    } else if (face.owner() == 0) {
      leftFaceId = face.id();
    } else {
      rightFaceId = face.id();
    }
  }

  SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  predictorFlux[leftFaceId] = -1.0;
  predictorFlux[internalFaceId] = 0.6;
  predictorFlux[rightFaceId] = 1.0;

  const auto predictorContinuity = evaluateContinuity(mesh, predictorFlux);
  EXPECT_NEAR(predictorContinuity.cellImbalance[0], -0.4, 1e-12);
  EXPECT_NEAR(predictorContinuity.cellImbalance[1], 0.4, 1e-12);

  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const PressureCorrectionAssembly assembly = assemblePressureCorrection(
      mesh, predictorFlux, responseCoefficient, responseCoefficient, /*density=*/1.0,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(mesh));

  // RHS: -imbalance, with row 0 forced to 0.
  EXPECT_NEAR(assembly.system.rhs()[0], 0.0, 1e-12);
  EXPECT_NEAR(assembly.system.rhs()[1], -0.4, 1e-12);
  EXPECT_NEAR(assembly.faceCoefficient[internalFaceId], 1.0, 1e-12);

  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto result = solver.solve(assembly.system);
  ASSERT_TRUE(result.converged());
  EXPECT_NEAR(result.solution[0], 0.0, 1e-10);
  EXPECT_NEAR(result.solution[1], -0.4, 1e-10);

  const ScalarField pPrime = [&] {
    ScalarField field(2);
    field[0] = result.solution[0];
    field[1] = result.solution[1];
    return field;
  }();
  const SurfaceField correctedFlux =
      correctFaceMassFlux(mesh, predictorFlux, assembly.faceCoefficient, pPrime);

  EXPECT_NEAR(correctedFlux[internalFaceId], 1.0, 1e-10);
  EXPECT_NEAR(correctedFlux[leftFaceId], -1.0, 1e-12);  // boundary flux untouched
  EXPECT_NEAR(correctedFlux[rightFaceId], 1.0, 1e-12);  // boundary flux untouched

  const auto correctedContinuity = evaluateContinuity(mesh, correctedFlux);
  EXPECT_NEAR(correctedContinuity.cellImbalance[0], 0.0, 1e-9);
  EXPECT_NEAR(correctedContinuity.cellImbalance[1], 0.0, 1e-9);
}

TEST(PressureCorrectionTest, ReferenceRowIsExactIdentity) {
  const Mesh mesh = makeTwoCellMesh();
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);

  const auto assembly =
      assemblePressureCorrection(mesh, predictorFlux, responseCoefficient, responseCoefficient, 1.0,
                                 0, makeZeroGradientPressureBoundaries(mesh));
  const auto& matrix = assembly.system.matrix();

  Vector e0(2, 0.0);
  e0[0] = 1.0;
  Vector e1(2, 0.0);
  e1[1] = 1.0;
  EXPECT_NEAR(matrix.multiply(e0)[0], 1.0, 1e-12);  // A(0,0) = 1
  EXPECT_NEAR(matrix.multiply(e1)[0], 0.0, 1e-12);  // A(0,1) = 0
  EXPECT_NEAR(assembly.system.rhs()[0], 0.0, 1e-12);
}

TEST(PressureCorrectionTest, ConstantResponseCoefficientGivesSymmetricInternalCoupling) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ScalarField responseCoefficient(mesh.numberOfCells(), 2.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);

  const auto assembly =
      assemblePressureCorrection(mesh, predictorFlux, responseCoefficient, responseCoefficient, 1.5,
                                 /*referenceCell=*/8, makeZeroGradientPressureBoundaries(mesh));
  const auto& matrix = assembly.system.matrix();

  // Cells 0 and 1 (bottom row, non-reference) share an internal face.
  Vector e0(9, 0.0);
  e0[0] = 1.0;
  Vector e1(9, 0.0);
  e1[1] = 1.0;
  EXPECT_NEAR(matrix.multiply(e0)[1], matrix.multiply(e1)[0], 1e-12);
  EXPECT_LT(matrix.multiply(e1)[0], 0.0);
}

TEST(PressureCorrectionTest, NonReferenceRowsConserveConstantInput) {
  // TODO.md section 54: before gauge fixing, constant p' -> zero flux
  // correction is a property of every row EXCEPT the forced reference
  // row (which by construction maps p'=1 -> 1, not 0).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  const Index referenceCell = 0;

  const auto assembly =
      assemblePressureCorrection(mesh, predictorFlux, responseCoefficient, responseCoefficient, 1.0,
                                 referenceCell, makeZeroGradientPressureBoundaries(mesh));
  const auto& matrix = assembly.system.matrix();

  const Vector ones(mesh.numberOfCells(), 1.0);
  const Vector rowSums = matrix.multiply(ones);
  for (Index i = 0; i < rowSums.size(); ++i) {
    if (i == referenceCell) {
      EXPECT_NEAR(rowSums[i], 1.0, 1e-12);
    } else {
      EXPECT_NEAR(rowSums[i], 0.0, 1e-12);
    }
  }
}

TEST(PressureCorrectionTest, BoundaryFacesGetZeroCoefficient) {
  const Mesh mesh = makeTwoCellMesh();
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);

  const auto assembly =
      assemblePressureCorrection(mesh, predictorFlux, responseCoefficient, responseCoefficient, 1.0,
                                 0, makeZeroGradientPressureBoundaries(mesh));
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      EXPECT_NEAR(assembly.faceCoefficient[face.id()], 0.0, 1e-12);
    }
  }
}

TEST(PressureCorrectionTest, MismatchedReferenceCellThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);

  EXPECT_THROW((void)assemblePressureCorrection(mesh, predictorFlux, responseCoefficient,
                                                responseCoefficient, 1.0, mesh.numberOfCells() + 5,
                                                makeZeroGradientPressureBoundaries(mesh)),
               InvalidArgumentError);
}

TEST(PressureCorrectionTest, NonPositiveDensityThrows) {
  const Mesh mesh = makeTwoCellMesh();
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);

  EXPECT_THROW((void)assemblePressureCorrection(mesh, predictorFlux, responseCoefficient,
                                                responseCoefficient, 0.0, 0,
                                                makeZeroGradientPressureBoundaries(mesh)),
               InvalidArgumentError);
}

// This session's addition: a FixedValue (Dirichlet) pressure patch gives
// a real, non-zero pressure-correction coupling instead of this file's
// original zero-everywhere boundary treatment -- see the header comment
// for why. Hand-derivation (mirroring TwoCellProbeSourceSignAndSolution-
// MatchHandDerivation's style): 2 unit cells (Af=1, dPN=1 between
// centers, half-cell distance 0.5 from a cell center to its own boundary
// face), response=1, density=1, left boundary Neumann ("inlet"), right
// boundary Dirichlet p'=0 ("outlet").
//
//   dCoefficient_internal = density*Af*response/dPN       = 1*1*1/1   = 1.0
//   dCoefficient_right    = density*Af*response/hP(=0.5)  = 1*1*1/0.5 = 2.0
//
// Fluxes: left=-1.0 (inflow), internal=0.6, right=1.2 (outflow exceeds
// inflow by 0.2 -- exactly the kind of global mismatch a closed/Neumann
// domain cannot absorb, per the header comment, but this open domain can).
//
//   cellImbalance[0] = -1.0 + 0.6 = -0.4  ->  rhs[0] =  0.4
//   cellImbalance[1] = -0.6 + 1.2 =  0.6  ->  rhs[1] = -0.6
//
// No referenceCell forcing (a FixedValue patch exists), so both rows get
// their full, real contributions:
//   row0:  1.0*p0' - 1.0*p1' =  0.4
//   row1: -1.0*p0' + 3.0*p1' = -0.6   (3.0 = 1.0 internal + 2.0 right)
// Solving: p1' = -0.1, p0' = 0.3.
TEST(PressureCorrectionTest, OpenBoundaryDirichletCouplingMatchesHandDerivation) {
  const Mesh mesh = makeTwoCellMesh();
  // makeTwoCellMesh is 2x1, so "left"/"right" each own exactly one face,
  // but "bottom"/"top" each own two (one per cell) -- unlike the
  // pre-existing two-cell tests above (whose all-zero-coupling closed
  // treatment made owner-id-based face identification harmless even when
  // it silently picked a bottom/top face instead), this test needs the
  // *actual* left/right faces since only "right" is Dirichlet here.
  auto singleFaceOfPatch = [&](const char* patchName) {
    for (const auto& patch : mesh.boundaryPatches()) {
      if (patch.name() == patchName) return patch.faceIds().front();
    }
    ADD_FAILURE() << "no such patch: " << patchName;
    return Index{0};
  };
  const Index leftFaceId = singleFaceOfPatch("left");
  const Index rightFaceId = singleFaceOfPatch("right");
  Index internalFaceId = 0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) internalFaceId = face.id();
  }

  SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  predictorFlux[leftFaceId] = -1.0;
  predictorFlux[internalFaceId] = 0.6;
  predictorFlux[rightFaceId] = 1.2;

  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const auto assembly = assemblePressureCorrection(
      mesh, predictorFlux, responseCoefficient, responseCoefficient,
      /*density=*/1.0, /*referenceCell=*/0, makeOpenChannelPressureBoundaries(mesh));

  EXPECT_NEAR(assembly.faceCoefficient[internalFaceId], 1.0, 1e-12);
  EXPECT_NEAR(assembly.faceCoefficient[rightFaceId], 2.0, 1e-12);
  EXPECT_NEAR(assembly.faceCoefficient[leftFaceId], 0.0, 1e-12);  // Neumann: still zero.

  EXPECT_NEAR(assembly.system.rhs()[0], 0.4, 1e-12);
  EXPECT_NEAR(assembly.system.rhs()[1], -0.6, 1e-12);

  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto result = solver.solve(assembly.system);
  ASSERT_TRUE(result.converged());
  EXPECT_NEAR(result.solution[0], 0.3, 1e-9);
  EXPECT_NEAR(result.solution[1], -0.1, 1e-9);

  const ScalarField pPrime = [&] {
    ScalarField field(2);
    field[0] = result.solution[0];
    field[1] = result.solution[1];
    return field;
  }();
  const SurfaceField corrected =
      correctFaceMassFlux(mesh, predictorFlux, assembly.faceCoefficient, pPrime);
  EXPECT_NEAR(corrected[leftFaceId], -1.0, 1e-9);  // Neumann boundary: untouched.
  EXPECT_NEAR(corrected[internalFaceId], 1.0, 1e-9);
  EXPECT_NEAR(corrected[rightFaceId], 1.0, 1e-9);  // Now matches inflow exactly.

  const auto correctedContinuity = evaluateContinuity(mesh, corrected);
  EXPECT_NEAR(correctedContinuity.cellImbalance[0], 0.0, 1e-9);
  EXPECT_NEAR(correctedContinuity.cellImbalance[1], 0.0, 1e-9);
  EXPECT_NEAR(correctedContinuity.globalNetFlux, 0.0, 1e-9);
}

TEST(PressureCorrectionTest, OpenBoundaryDoesNotForceReferenceCellRow) {
  // With a FixedValue pressure patch present, referenceCell must NOT be
  // forced to an identity row (see header comment: that would over-
  // constrain an already well-posed system) -- contrast with
  // ReferenceRowIsExactIdentity above, whose all-Neumann boundary set
  // still forces it.
  const Mesh mesh = makeTwoCellMesh();
  const ScalarField responseCoefficient(mesh.numberOfCells(), 1.0);
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);

  const auto assembly =
      assemblePressureCorrection(mesh, predictorFlux, responseCoefficient, responseCoefficient, 1.0,
                                 /*referenceCell=*/0, makeOpenChannelPressureBoundaries(mesh));
  const auto& matrix = assembly.system.matrix();

  Vector e0(2, 0.0);
  e0[0] = 1.0;
  Vector e1(2, 0.0);
  e1[1] = 1.0;
  // Row 0 = internal coupling only (dCoefficient=1.0), NOT the [1, 0]
  // identity row ReferenceRowIsExactIdentity checks for the closed case.
  EXPECT_NEAR(matrix.multiply(e0)[0], 1.0, 1e-12);
  EXPECT_NEAR(matrix.multiply(e1)[0], -1.0, 1e-12);
}
