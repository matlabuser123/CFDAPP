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

}  // namespace

// --- The transient diagonal actually reaches the response coefficient -----

TEST(TransientPressureCorrectionTest, ResponseCoefficientReflectsTransientDiagonalExactly) {
  // Mirrors PISO-B's own DiagonalIncludesExactTransientCoefficient
  // technique: an independently-assembled base-physics-only diagonal,
  // compared against assembleTransientMomentumComponent's actual output,
  // rather than hand-deriving diffusion/convection from scratch (already
  // covered by MomentumEquation's own tests).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const Real dt = 0.02;

  const auto uAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);
  const ScalarField dU = computeMomentumResponseCoefficient(mesh, uAssembly.diagonal);

  for (const auto& cell : mesh.cells()) {
    const Real expectedAP = uAssembly.diagonal[cell.id()];  // already includes rho*V/dt
    const Real expectedD = cell.volume() / expectedAP;
    EXPECT_NEAR(dU[cell.id()], expectedD, 1e-12) << "at cell " << cell.id();
    // Sanity: the transient term alone is a real, non-negligible part of
    // aP here (rho*V/dt = 1*(1/16)/0.02 = 3.125, versus a modest
    // diffusion-only diagonal at mu=0.01) -- this is not a no-op check.
    EXPECT_GT(expectedAP, (fluid.density() * cell.volume()) / dt);
  }
}

TEST(TransientPressureCorrectionTest, SmallerDeltaTGivesSmallerResponseCoefficientEverywhere) {
  // "smaller dt -> larger rho*V/dt -> larger aP -> smaller d" -- checked
  // against the *real* transient momentum diagonal for two different dt,
  // same base state, proving this composition genuinely uses PISO's own
  // predictor diagonal rather than silently falling back to some steady
  // value (TODO.md P2 -- PISO-D notes).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);

  const auto largerDtAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, /*dt=*/0.02);
  const auto smallerDtAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, /*dt=*/0.005);

  const ScalarField dLargerDt = computeMomentumResponseCoefficient(mesh, largerDtAssembly.diagonal);
  const ScalarField dSmallerDt =
      computeMomentumResponseCoefficient(mesh, smallerDtAssembly.diagonal);

  for (const auto& cell : mesh.cells()) {
    EXPECT_GT(smallerDtAssembly.diagonal[cell.id()], largerDtAssembly.diagonal[cell.id()])
        << "aP at cell " << cell.id();
    EXPECT_LT(dSmallerDt[cell.id()], dLargerDt[cell.id()]) << "d at cell " << cell.id();
  }
}

// --- Two-cell hand-derived probe, transient-diagonal-derived d ------------

// A hand-picked, round "base" (spatial-only) momentum diagonal stands in
// for whatever assembleTransientMomentumComponent's diffusion+convection
// terms would actually produce -- that composition is already proven
// exactly by TransientMomentumAssemblyTest.DiagonalIncludesExactTransient
// Coefficient (PISO-B); this probe's job is the *pressure-correction*
// side: does a d built from (base + rho*V/dt) propagate correctly through
// assemblePressureCorrection and change p' by exactly the expected
// factor.
TEST(TransientPressureCorrectionTest, TwoCellProbeWithTransientDiagonalMatchesHandDerivation) {
  // 2 unit cells (V=1 each), aP_base=2.0 (hand-picked), rho=1, dt=0.5
  // -> rho*V/dt = 1*1/0.5 = 2.0 -> aP_total = 4.0 -> d = V/aP = 0.25.
  //
  // Same F* as the validated SIMPLE probe
  // (PressureCorrectionTest.TwoCellProbeSourceSignAndSolutionMatchHand
  // Derivation): left=-1.0 (inflow), internal=0.6, right=1.0 (outflow) ->
  // cellImbalance = [-0.4, +0.4] -> rhs (pre-reference) = [0.4, -0.4].
  // Reference cell 0 forced to p'=0, so rhs becomes [0, -0.4].
  //
  // D_f (internal) = rho*Af*d/dPN = 1*1*0.25/1 = 0.25 (versus 1.0 in the
  // original d=1.0 probe -- the whole point of this test: a 4x larger aP
  // gives a 4x smaller D_f).
  //   row1: D_f*p1' = rhs[1]  =>  0.25*p1' = -0.4  =>  p1' = -1.6
  // (4x the original probe's p1'=-0.4, exactly tracking d's 4x decrease --
  // this relationship, not just the raw numbers, is what proves d is
  // actually driving the correction.)
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
    momentumDiagonal[cell.id()] = aPBase + (density * cell.volume() / dt);  // = 4.0
  }
  const ScalarField responseCoefficient =
      computeMomentumResponseCoefficient(mesh, momentumDiagonal);
  ASSERT_NEAR(responseCoefficient[0], 0.25, 1e-12);
  ASSERT_NEAR(responseCoefficient[1], 0.25, 1e-12);

  SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  predictorFlux[leftFaceId] = -1.0;
  predictorFlux[internalFaceId] = 0.6;
  predictorFlux[rightFaceId] = 1.0;

  const auto predictorContinuity = evaluateContinuity(mesh, predictorFlux);
  EXPECT_NEAR(predictorContinuity.cellImbalance[0], -0.4, 1e-12);
  EXPECT_NEAR(predictorContinuity.cellImbalance[1], 0.4, 1e-12);

  const auto assembly = assemblePressureCorrection(
      mesh, predictorFlux, responseCoefficient, responseCoefficient, density, /*referenceCell=*/0,
      makeZeroGradientPressureBoundaries(mesh));

  EXPECT_NEAR(assembly.system.rhs()[0], 0.0, 1e-12);
  EXPECT_NEAR(assembly.system.rhs()[1], -0.4, 1e-12);
  EXPECT_NEAR(assembly.faceCoefficient[internalFaceId], 0.25, 1e-12);

  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto result = solver.solve(assembly.system);
  ASSERT_TRUE(result.converged());
  EXPECT_NEAR(result.solution[0], 0.0, 1e-9);
  EXPECT_NEAR(result.solution[1], -1.6, 1e-9);
}

// --- Full predictor -> pressure-correction-#1 pipeline, then STOP --------
//
// No PISO class exists yet (deliberately -- PISO-H). This composes the
// already-verified pieces exactly the way one eventually will: transient
// momentum predictor (PISO-B) -> predictor face flux via the same
// authoritative calculateMassFlux (PISO-C) -> response coefficients from
// the *actual* transient diagonal -> pressure-correction #1 assembly +
// solve (this task). It does not correct pressure, velocity, or face
// flux -- that is PISO-E, not here.

namespace {

struct PredictorAndCorrectionOne {
  VectorField predictorVelocity;
  SurfaceField predictorFlux;
  ScalarField pPrime;
  bool momentumConverged;
  bool pressureConverged;
};

PredictorAndCorrectionOne runPredictorThroughFirstPressureCorrection(
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

  PredictorAndCorrectionOne out;
  out.momentumConverged = uResult.converged() && vResult.converged();
  if (!out.momentumConverged) return out;

  out.predictorVelocity = VectorField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.predictorVelocity[cell.id()] =
        Vector2{uResult.solution[cell.id()], vResult.solution[cell.id()]};
  }
  // "Do not introduce an independent PISO flux convention" (PISO-C).
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
  return out;
}

}  // namespace

TEST(TransientPressureCorrectionTest, FullPipelineFromRestReturnsFinitePressureCorrection) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runPredictorThroughFirstPressureCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureConverged);
  for (Index i = 0; i < out.pPrime.size(); ++i) {
    EXPECT_TRUE(std::isfinite(out.pPrime[i])) << "cell " << i;
  }
}

TEST(TransientPressureCorrectionTest, PreviousStateAndInputsAreNotMutatedByThePipeline) {
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
  const VectorField velocityCopy = velocity;
  const ScalarField pressureCopy = pressure;
  const SurfaceField massFluxCopy = massFlux;

  const auto out = runPredictorThroughFirstPressureCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);
  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureConverged);

  for (Index i = 0; i < previousU.size(); ++i) {
    EXPECT_DOUBLE_EQ(previousU[i], previousUCopy[i]);
    EXPECT_DOUBLE_EQ(previousV[i], previousVCopy[i]);
    EXPECT_DOUBLE_EQ(pressure[i], pressureCopy[i]);
    EXPECT_DOUBLE_EQ(velocity[i].x, velocityCopy[i].x);
    EXPECT_DOUBLE_EQ(velocity[i].y, velocityCopy[i].y);
  }
  for (Index i = 0; i < massFlux.size(); ++i) {
    EXPECT_DOUBLE_EQ(massFlux[i], massFluxCopy[i]);
  }
}

TEST(TransientPressureCorrectionTest, RepeatedPipelineRunIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto outA = runPredictorThroughFirstPressureCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);
  const auto outB = runPredictorThroughFirstPressureCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(outA.pressureConverged);
  ASSERT_TRUE(outB.pressureConverged);
  ASSERT_EQ(outA.pPrime.size(), outB.pPrime.size());
  for (Index i = 0; i < outA.pPrime.size(); ++i) {
    EXPECT_EQ(outA.pPrime[i], outB.pPrime[i]);
  }
}
