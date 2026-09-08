#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::evaluateContinuity;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::assembleTransientMomentumComponent;
using cfd::pressure_velocity::computeMomentumResponseCoefficient;
using cfd::pressure_velocity::correctFaceMassFlux;
using cfd::pressure_velocity::correctVelocity;

namespace {

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh, Vector2 lidVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(lidVelocity));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

BiCGSTAB makeProbeSolver() {
  LinearSolverSettings settings;
  settings.maxIterations = 500;
  settings.absoluteTolerance = 1e-12;
  settings.relativeTolerance = 1e-10;
  return BiCGSTAB(settings);
}

// The full first-correction step (PISO-D's predictor -> p'1, plus PISO-E's
// application of it): transient momentum predictor -> predictor flux ->
// pressure-correction #1 assembly+solve -> apply p'/U/F correction #1 ->
// STOP. No second correction, no PISO class -- see TODO.md P2 -- PISO-E
// notes. Retains predictor AND corrected quantities as distinct fields
// (never overwrites one with the other), so a caller -- here, a test --
// can inspect both independently.
struct FirstCorrectionStepResult {
  VectorField predictorVelocity;
  SurfaceField predictorFlux;
  ScalarField pPrime;
  ScalarField correctedPressure;
  VectorField correctedVelocity;
  SurfaceField correctedFlux;
  Real predictorContinuityRms{};
  Real correctedContinuityRms{};
  Real predictorGlobalImbalance{};
  Real correctedGlobalImbalance{};
  bool momentumConverged = false;
  bool pressureConverged = false;
};

FirstCorrectionStepResult runFirstCorrectionStep(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& pressure,
    const SurfaceField& massFlux, const FluidProperties& fluid,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& pressureBoundaries,
    const ScalarField& previousU, const ScalarField& previousV, Real dt, Index referenceCell) {
  const auto uAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);
  const auto vAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::V, previousV, dt);

  const BiCGSTAB solver = makeProbeSolver();
  const auto uResult = solver.solve(uAssembly.system);
  const auto vResult = solver.solve(vAssembly.system);

  FirstCorrectionStepResult out;
  out.momentumConverged = uResult.converged() && vResult.converged();
  if (!out.momentumConverged) return out;

  out.predictorVelocity = VectorField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.predictorVelocity[cell.id()] =
        Vector2{uResult.solution[cell.id()], vResult.solution[cell.id()]};
  }
  out.predictorFlux = calculateMassFlux(mesh, out.predictorVelocity, fluid, velocityBoundaries);

  const ScalarField dU = computeMomentumResponseCoefficient(mesh, uAssembly.diagonal);
  const ScalarField dV = computeMomentumResponseCoefficient(mesh, vAssembly.diagonal);
  const auto pAssembly = assemblePressureCorrection(
      mesh, out.predictorFlux, dU, dV, fluid.density(), referenceCell, pressureBoundaries);
  const auto pResult = solver.solve(pAssembly.system);
  out.pressureConverged = pResult.converged();
  if (!out.pressureConverged) return out;

  out.pPrime = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.pPrime[cell.id()] = pResult.solution[cell.id()];
  }

  // p1 = p* + p'1 -- deliberately NO SIMPLE-style pressure under-
  // relaxation (TODO.md P2 -- PISO-E notes: PISO is not a steady SIMPLE
  // outer iteration).
  out.correctedPressure = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.correctedPressure[cell.id()] = pressure[cell.id()] + out.pPrime[cell.id()];
  }
  // Same dU/dV the pressure-correction equation was assembled with --
  // "do not recompute a different approximation after solving p'."
  out.correctedVelocity =
      correctVelocity(mesh, out.predictorVelocity, dU, dV, out.pPrime, pressureBoundaries);
  // Correct the authoritative SurfaceField directly with the SAME
  // faceCoefficient the matrix was assembled with -- never regenerate
  // from interpolated corrected cell velocities.
  out.correctedFlux =
      correctFaceMassFlux(mesh, out.predictorFlux, pAssembly.faceCoefficient, out.pPrime);

  const auto predictorContinuity = evaluateContinuity(mesh, out.predictorFlux);
  const auto correctedContinuity = evaluateContinuity(mesh, out.correctedFlux);
  Real predictorSumSquares = 0.0, correctedSumSquares = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    predictorSumSquares +=
        predictorContinuity.cellImbalance[i] * predictorContinuity.cellImbalance[i];
    correctedSumSquares +=
        correctedContinuity.cellImbalance[i] * correctedContinuity.cellImbalance[i];
  }
  out.predictorContinuityRms =
      std::sqrt(predictorSumSquares / static_cast<Real>(mesh.numberOfCells()));
  out.correctedContinuityRms =
      std::sqrt(correctedSumSquares / static_cast<Real>(mesh.numberOfCells()));
  out.predictorGlobalImbalance = std::abs(predictorContinuity.globalNetFlux);
  out.correctedGlobalImbalance = std::abs(correctedContinuity.globalNetFlux);

  return out;
}

}  // namespace

// --- Hand-derived two-cell probe: correction application ------------------

TEST(CorrectionApplicationTest, TwoCellProbeCorrectionMatchesHandDerivation) {
  // Continues PISO-D's TwoCellProbeWithTransientDiagonalMatchesHand
  // Derivation exactly: aP_base=2.0, rho=1, dt=0.5 -> d=0.25; same F*
  // (left=-1.0, internal=0.6, right=1.0); p0'=0, p1'=-1.6.
  //
  // Velocity correction (u_P = u*_P - d*(dp'/dx)_P): with p'=[0,-1.6] over
  // cell centroids at x=0.5 and x=1.5 (dx=1 between them), grad(p')_x at
  // each interior-adjacent... this 2-cell mesh has every face touching a
  // boundary except the middle one, so correctVelocity's own internal
  // Gauss-gradient boundary treatment (zero-gradient) applies at each
  // cell's own boundary faces -- rather than re-deriving that gradient by
  // hand (already exactly covered by VelocityCorrectionTest), this test
  // checks the *flux* correction, which has a direct, exactly-known
  // formula independent of the gradient scheme:
  //   F_internal' = D_f * (p'_0 - p'_1) = 0.25 * (0 - (-1.6)) = 0.4
  //   corrected internal flux = 0.6 + 0.4 = 1.0
  // and pressure:
  //   p1_0 = p*_0 + p'_0 = 0 + 0 = 0
  //   p1_1 = p*_1 + p'_1 = 0 + (-1.6) = -1.6
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
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

  const Real aPBase = 2.0;
  const Real density = 1.0;
  const Real dt = 0.5;
  Vector momentumDiagonal(2);
  for (const auto& cell : mesh.cells()) {
    momentumDiagonal[cell.id()] = aPBase + (density * cell.volume() / dt);
  }
  const ScalarField responseCoefficient =
      computeMomentumResponseCoefficient(mesh, momentumDiagonal);

  SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  predictorFlux[leftFaceId] = -1.0;
  predictorFlux[internalFaceId] = 0.6;
  predictorFlux[rightFaceId] = 1.0;

  const auto assembly = assemblePressureCorrection(
      mesh, predictorFlux, responseCoefficient, responseCoefficient, density, /*referenceCell=*/0,
      makeZeroGradientPressureBoundaries(mesh));
  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto pResult = solver.solve(assembly.system);
  ASSERT_TRUE(pResult.converged());
  ScalarField pPrime(2);
  pPrime[0] = pResult.solution[0];
  pPrime[1] = pResult.solution[1];
  ASSERT_NEAR(pPrime[0], 0.0, 1e-9);
  ASSERT_NEAR(pPrime[1], -1.6, 1e-9);

  const ScalarField predictorPressure(mesh.numberOfCells(), 0.0);
  ScalarField correctedPressure(2);
  for (const auto& cell : mesh.cells()) {
    correctedPressure[cell.id()] = predictorPressure[cell.id()] + pPrime[cell.id()];
  }
  EXPECT_NEAR(correctedPressure[0], 0.0, 1e-9);
  EXPECT_NEAR(correctedPressure[1], -1.6, 1e-9);

  const SurfaceField correctedFlux =
      correctFaceMassFlux(mesh, predictorFlux, assembly.faceCoefficient, pPrime);
  EXPECT_NEAR(correctedFlux[internalFaceId], 1.0, 1e-9);
  EXPECT_NEAR(correctedFlux[leftFaceId], -1.0, 1e-9);  // boundary: untouched
  EXPECT_NEAR(correctedFlux[rightFaceId], 1.0, 1e-9);  // boundary: untouched

  // The pressure-correction equation was assembled to drive this exact
  // 2-cell system's imbalance to zero -- a direct (non-iterative) 2x2
  // solve, so the corrected continuity is exact, not merely improved.
  const auto predictorContinuity = evaluateContinuity(mesh, predictorFlux);
  const auto correctedContinuity = evaluateContinuity(mesh, correctedFlux);
  EXPECT_NEAR(predictorContinuity.cellImbalance[0], -0.4, 1e-12);
  EXPECT_NEAR(predictorContinuity.cellImbalance[1], 0.4, 1e-12);
  EXPECT_NEAR(correctedContinuity.cellImbalance[0], 0.0, 1e-9);
  EXPECT_NEAR(correctedContinuity.cellImbalance[1], 0.0, 1e-9);
  EXPECT_LT(std::abs(correctedContinuity.cellImbalance[0]),
            std::abs(predictorContinuity.cellImbalance[0]));
}

TEST(CorrectionApplicationTest, ZeroPressureCorrectionLeavesEverythingUnchanged) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const VectorField predictorVelocity(mesh.numberOfCells(), Vector2{1.3, -0.4});
  const ScalarField predictorPressure(mesh.numberOfCells(), 5.0);
  SurfaceField predictorFlux(mesh.numberOfFaces());
  for (Index i = 0; i < predictorFlux.size(); ++i) predictorFlux[i] = 0.02 * static_cast<Real>(i);
  const ScalarField uResponse(mesh.numberOfCells(), 0.4);
  const ScalarField vResponse(mesh.numberOfCells(), 0.6);
  const ScalarField pPrime(mesh.numberOfCells(), 0.0);  // p' = 0 everywhere

  ScalarField correctedPressure(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    correctedPressure[cell.id()] = predictorPressure[cell.id()] + pPrime[cell.id()];
  }
  const VectorField correctedVelocity =
      correctVelocity(mesh, predictorVelocity, uResponse, vResponse, pPrime,
                      makeZeroGradientPressureBoundaries(mesh));
  const SurfaceField faceCoefficient(mesh.numberOfFaces(), 1.0);  // arbitrary, irrelevant when p'=0
  const SurfaceField correctedFlux =
      correctFaceMassFlux(mesh, predictorFlux, faceCoefficient, pPrime);

  for (const auto& cell : mesh.cells()) {
    EXPECT_DOUBLE_EQ(correctedPressure[cell.id()], predictorPressure[cell.id()]);
    EXPECT_DOUBLE_EQ(correctedVelocity[cell.id()].x, predictorVelocity[cell.id()].x);
    EXPECT_DOUBLE_EQ(correctedVelocity[cell.id()].y, predictorVelocity[cell.id()].y);
  }
  for (Index i = 0; i < predictorFlux.size(); ++i) {
    EXPECT_DOUBLE_EQ(correctedFlux[i], predictorFlux[i]);
  }
}

// --- Full pipeline: real transient predictor through correction #1 --------

TEST(CorrectionApplicationTest, FullPipelineFromRestProducesFiniteCorrectedState) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                          velocityBoundaries, pressureBoundaries, previousU,
                                          previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_TRUE(std::isfinite(out.correctedPressure[i])) << "cell " << i;
    EXPECT_TRUE(std::isfinite(out.correctedVelocity[i].x)) << "cell " << i;
    EXPECT_TRUE(std::isfinite(out.correctedVelocity[i].y)) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_TRUE(std::isfinite(out.correctedFlux[i])) << "face " << i;
  }
}

TEST(CorrectionApplicationTest, ContinuityImprovesAfterCorrection) {
  // "Rc1 < Rc*" -- the whole point of solving and applying p'1.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                          velocityBoundaries, pressureBoundaries, previousU,
                                          previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureConverged);
  EXPECT_LT(out.correctedContinuityRms, out.predictorContinuityRms);
  EXPECT_LE(out.correctedGlobalImbalance, out.predictorGlobalImbalance);
  // Reference-cell forcing makes this a closed-cavity system (no
  // FixedValue pressure patch), so correction should drive the global
  // imbalance to near machine zero, the same invariant
  // SIMPLEContinuityTest.GlobalMassImbalanceStaysNearZeroForClosedDomain
  // rests on for a converged SIMPLE solve -- here after a single PISO
  // correction, not many SIMPLE outer iterations.
  EXPECT_NEAR(out.correctedGlobalImbalance, 0.0, 1e-9);
}

TEST(CorrectionApplicationTest, WallNormalBoundaryFluxStaysExactlyZeroAfterCorrection) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                          velocityBoundaries, pressureBoundaries, previousU,
                                          previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureConverged);
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(out.correctedFlux[faceId], 0.0, 1e-9) << "boundary face " << faceId;
    }
  }
}

TEST(CorrectionApplicationTest, PreviousStateAndPredictorFieldsRetainedIndependently) {
  // "predictor input can be retained independently if API promises that"
  // -- this test-local pipeline's own promise: predictorVelocity/
  // predictorFlux are never overwritten by the corrected fields.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  ScalarField previousU(mesh.numberOfCells(), 0.0);
  ScalarField previousV(mesh.numberOfCells(), 0.0);
  const ScalarField previousUCopy = previousU;
  const ScalarField previousVCopy = previousV;

  const auto out = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                          velocityBoundaries, pressureBoundaries, previousU,
                                          previousV, /*dt=*/0.01, /*referenceCell=*/0);
  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureConverged);

  for (Index i = 0; i < previousU.size(); ++i) {
    EXPECT_DOUBLE_EQ(previousU[i], previousUCopy[i]);
    EXPECT_DOUBLE_EQ(previousV[i], previousVCopy[i]);
  }
  // Predictor and corrected fields are genuinely distinct (not the
  // identical field aliased under two names) -- correction did real work.
  bool anyDifference = false;
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    if (out.predictorFlux[i] != out.correctedFlux[i]) anyDifference = true;
  }
  EXPECT_TRUE(anyDifference);
}

TEST(CorrectionApplicationTest, RepeatedCorrectionStepIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto outA = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                           velocityBoundaries, pressureBoundaries, previousU,
                                           previousV, /*dt=*/0.01, /*referenceCell=*/0);
  const auto outB = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                           velocityBoundaries, pressureBoundaries, previousU,
                                           previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(outA.pressureConverged);
  ASSERT_TRUE(outB.pressureConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(outA.correctedPressure[i], outB.correctedPressure[i]);
    EXPECT_EQ(outA.correctedVelocity[i].x, outB.correctedVelocity[i].x);
    EXPECT_EQ(outA.correctedVelocity[i].y, outB.correctedVelocity[i].y);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(outA.correctedFlux[i], outB.correctedFlux[i]);
  }
}
