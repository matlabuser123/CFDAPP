#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>

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

// Predictor -> p'1 -> apply correction #1 -> STOP (PISO-E's pipeline,
// reproduced locally per this test suite's own established precedent of
// not sharing helpers across files -- see test_correction_application.cpp).
struct FirstCorrectionStepResult {
  VectorField predictorVelocity;
  SurfaceField predictorFlux;
  ScalarField dU;
  ScalarField dV;
  std::optional<cfd::pressure_velocity::PressureCorrectionAssembly> pAssemblyOne;
  ScalarField pPrimeOne;
  ScalarField correctedPressure;
  VectorField correctedVelocity;
  SurfaceField correctedFlux;
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

  out.dU = computeMomentumResponseCoefficient(mesh, uAssembly.diagonal);
  out.dV = computeMomentumResponseCoefficient(mesh, vAssembly.diagonal);
  out.pAssemblyOne = assemblePressureCorrection(mesh, out.predictorFlux, out.dU, out.dV,
                                                fluid.density(), referenceCell, pressureBoundaries);
  const auto pResult = solver.solve(out.pAssemblyOne->system);
  out.pressureConverged = pResult.converged();
  if (!out.pressureConverged) return out;

  out.pPrimeOne = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.pPrimeOne[cell.id()] = pResult.solution[cell.id()];
  }

  out.correctedPressure = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.correctedPressure[cell.id()] = pressure[cell.id()] + out.pPrimeOne[cell.id()];
  }
  out.correctedVelocity = correctVelocity(mesh, out.predictorVelocity, out.dU, out.dV,
                                          out.pPrimeOne, pressureBoundaries);
  out.correctedFlux = correctFaceMassFlux(mesh, out.predictorFlux,
                                          out.pAssemblyOne->faceCoefficient, out.pPrimeOne);
  return out;
}

// PISO-F: from FirstCorrectionStepResult, assemble + solve pressure
// correction #2 from F1 (the corrected flux, NOT the original predictor
// F*), reusing the SAME dU/dV (hence the same faceCoefficient) correction
// #1 used -- no momentum reassembly between correctors. Does not apply
// p'2 to anything; that is PISO-G.
struct SecondCorrectionAssemblyResult {
  std::optional<cfd::pressure_velocity::PressureCorrectionAssembly> pAssemblyTwo;
  ScalarField pPrimeTwo;
  bool secondPressureConverged = false;
};

SecondCorrectionAssemblyResult runSecondPressureCorrectionAssembly(
    const Mesh& mesh, const FirstCorrectionStepResult& first,
    const BoundaryConditionSet& pressureBoundaries, Real density, Index referenceCell) {
  SecondCorrectionAssemblyResult out;
  out.pAssemblyTwo = assemblePressureCorrection(mesh, first.correctedFlux, first.dU, first.dV,
                                                density, referenceCell, pressureBoundaries);
  const BiCGSTAB solver = makeProbeSolver();
  const auto pResult = solver.solve(out.pAssemblyTwo->system);
  out.secondPressureConverged = pResult.converged();
  if (!out.secondPressureConverged) return out;
  out.pPrimeTwo = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.pPrimeTwo[cell.id()] = pResult.solution[cell.id()];
  }
  return out;
}

}  // namespace

// --- Hand-derived two-cell probes: correction #2 assembly -----------------
//
// Same 2-cell topology as PISO-D/E's probes: V=1 each, aP_base=2.0,
// rho=1, dt=0.5 -> d=0.25 -> D_f(internal)=0.25, D_f(boundary)=0. F* =
// [left=-1.0, internal=0.6, right=1.0] -> Rc* (predictor imbalance) =
// [-0.4, +0.4] -> full converged p'1 = [0, -1.6] (PISO-D).

namespace {

struct TwoCellProbeGeometry {
  Mesh mesh;
  Index internalFaceId = 0;
  Index leftFaceId = 0;
  Index rightFaceId = 0;
  ScalarField responseCoefficient;
};

TwoCellProbeGeometry makeTwoCellProbeGeometry() {
  TwoCellProbeGeometry g{MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0), 0, 0, 0, ScalarField(2)};
  for (const auto& face : g.mesh.faces()) {
    if (!face.isBoundary()) {
      g.internalFaceId = face.id();
    } else if (face.owner() == 0) {
      g.leftFaceId = face.id();
    } else {
      g.rightFaceId = face.id();
    }
  }
  const Real aPBase = 2.0;
  const Real density = 1.0;
  const Real dt = 0.5;
  Vector momentumDiagonal(2);
  for (const auto& cell : g.mesh.cells()) {
    momentumDiagonal[cell.id()] = aPBase + (density * cell.volume() / dt);  // = 4.0
  }
  g.responseCoefficient = computeMomentumResponseCoefficient(g.mesh, momentumDiagonal);
  return g;
}

}  // namespace

TEST(SecondPressureCorrectionTest, PartialFirstCorrectionGivesHandDerivedNonzeroSecondCorrection) {
  // Correction #1 is deliberately only HALF applied (p'1_half = [0,-0.8],
  // half of the true converged [0,-1.6]) -- simulating an incompletely
  // converged (or otherwise incomplete) first corrector, without relying
  // on any solver-iteration-count fragility. This makes Rc1 hand-known
  // and nonzero, and lets PISO-F's second correction be verified bit-
  // exactly against hand arithmetic.
  const auto g = makeTwoCellProbeGeometry();
  const Real density = 1.0;

  SurfaceField predictorFlux(g.mesh.numberOfFaces(), 0.0);
  predictorFlux[g.leftFaceId] = -1.0;
  predictorFlux[g.internalFaceId] = 0.6;
  predictorFlux[g.rightFaceId] = 1.0;

  ScalarField pPrimeHalf(2);
  pPrimeHalf[0] = 0.0;
  pPrimeHalf[1] = -0.8;

  const auto pAssemblyOne = assemblePressureCorrection(
      g.mesh, predictorFlux, g.responseCoefficient, g.responseCoefficient, density,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(g.mesh));
  ASSERT_NEAR(pAssemblyOne.system.rhs()[0], 0.0, 1e-12);
  ASSERT_NEAR(pAssemblyOne.system.rhs()[1], -0.4, 1e-12);  // RHS1 = -Rc*

  const SurfaceField correctedFluxOne =
      correctFaceMassFlux(g.mesh, predictorFlux, pAssemblyOne.faceCoefficient, pPrimeHalf);
  EXPECT_NEAR(correctedFluxOne[g.internalFaceId], 0.8, 1e-12);
  EXPECT_NEAR(correctedFluxOne[g.leftFaceId], -1.0, 1e-12);
  EXPECT_NEAR(correctedFluxOne[g.rightFaceId], 1.0, 1e-12);

  const auto rc1 = evaluateContinuity(g.mesh, correctedFluxOne);
  EXPECT_NEAR(rc1.cellImbalance[0], -0.2, 1e-12);
  EXPECT_NEAR(rc1.cellImbalance[1], 0.2, 1e-12);
  // Rc1 improved relative to Rc* but is genuinely, deliberately nonzero.
  EXPECT_LT(std::abs(rc1.cellImbalance[0]), 0.4);
  EXPECT_GT(std::abs(rc1.cellImbalance[0]), 0.0);

  const auto pAssemblyTwo = assemblePressureCorrection(
      g.mesh, correctedFluxOne, g.responseCoefficient, g.responseCoefficient, density,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(g.mesh));
  // RHS2 = -Rc1, NOT -Rc* -- the whole point of PISO-F.
  EXPECT_NEAR(pAssemblyTwo.system.rhs()[0], 0.0, 1e-12);
  EXPECT_NEAR(pAssemblyTwo.system.rhs()[1], -0.2, 1e-12);
  // Same D_f as correction #1 -- same dU/dV, no momentum reassembly.
  EXPECT_NEAR(pAssemblyTwo.faceCoefficient[g.internalFaceId],
              pAssemblyOne.faceCoefficient[g.internalFaceId], 1e-12);

  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto pResultTwo = solver.solve(pAssemblyTwo.system);
  ASSERT_TRUE(pResultTwo.converged());
  EXPECT_NEAR(pResultTwo.solution[0], 0.0, 1e-9);
  EXPECT_NEAR(pResultTwo.solution[1], -0.8, 1e-9);  // hand-derived: p'2 = [0, -0.8]

  // Sanity: p'1_half + p'2 reconstructs the original fully-converged p'1
  // = [0, -1.6] from PISO-D exactly -- two half-corrections compose to
  // the one full correction, confirming the sign and scale are right.
  EXPECT_NEAR(pPrimeHalf[1] + pResultTwo.solution[1], -1.6, 1e-9);
}

TEST(SecondPressureCorrectionTest, FullFirstCorrectionGivesExactlyZeroSecondCorrection) {
  // "if correction #1 makes continuity exactly zero, RHS2 is
  // exactly/nearly zero" -- the fully-converged p'1 = [0,-1.6] from
  // PISO-D zeroes Rc1 exactly, so correction #2's RHS and solution should
  // both be ~0.
  const auto g = makeTwoCellProbeGeometry();
  const Real density = 1.0;

  SurfaceField predictorFlux(g.mesh.numberOfFaces(), 0.0);
  predictorFlux[g.leftFaceId] = -1.0;
  predictorFlux[g.internalFaceId] = 0.6;
  predictorFlux[g.rightFaceId] = 1.0;

  ScalarField pPrimeFull(2);
  pPrimeFull[0] = 0.0;
  pPrimeFull[1] = -1.6;

  const auto pAssemblyOne = assemblePressureCorrection(
      g.mesh, predictorFlux, g.responseCoefficient, g.responseCoefficient, density,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(g.mesh));
  const SurfaceField correctedFluxOne =
      correctFaceMassFlux(g.mesh, predictorFlux, pAssemblyOne.faceCoefficient, pPrimeFull);
  const auto rc1 = evaluateContinuity(g.mesh, correctedFluxOne);
  ASSERT_NEAR(rc1.cellImbalance[0], 0.0, 1e-12);
  ASSERT_NEAR(rc1.cellImbalance[1], 0.0, 1e-12);

  const auto pAssemblyTwo = assemblePressureCorrection(
      g.mesh, correctedFluxOne, g.responseCoefficient, g.responseCoefficient, density,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(g.mesh));
  EXPECT_NEAR(pAssemblyTwo.system.rhs()[0], 0.0, 1e-12);
  EXPECT_NEAR(pAssemblyTwo.system.rhs()[1], 0.0, 1e-12);

  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto pResultTwo = solver.solve(pAssemblyTwo.system);
  ASSERT_TRUE(pResultTwo.converged());
  EXPECT_NEAR(pResultTwo.solution[0], 0.0, 1e-9);
  EXPECT_NEAR(pResultTwo.solution[1], 0.0, 1e-9);
}

TEST(SecondPressureCorrectionTest, DeterministicReferenceCellRowIsPreserved) {
  // The reference-cell row-forcing convention (p'[referenceCell] = 0) is
  // reused unchanged for correction #2, matching PISO-D's own convention
  // exactly, regardless of what Rc1's source at that cell is.
  const auto g = makeTwoCellProbeGeometry();
  const Real density = 1.0;
  SurfaceField predictorFlux(g.mesh.numberOfFaces(), 0.0);
  predictorFlux[g.leftFaceId] = -1.0;
  predictorFlux[g.internalFaceId] = 0.6;
  predictorFlux[g.rightFaceId] = 1.0;
  ScalarField pPrimeHalf(2);
  pPrimeHalf[0] = 0.0;
  pPrimeHalf[1] = -0.8;
  const auto pAssemblyOne = assemblePressureCorrection(
      g.mesh, predictorFlux, g.responseCoefficient, g.responseCoefficient, density,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(g.mesh));
  const SurfaceField correctedFluxOne =
      correctFaceMassFlux(g.mesh, predictorFlux, pAssemblyOne.faceCoefficient, pPrimeHalf);

  const auto pAssemblyTwo = assemblePressureCorrection(
      g.mesh, correctedFluxOne, g.responseCoefficient, g.responseCoefficient, density,
      /*referenceCell=*/0, makeZeroGradientPressureBoundaries(g.mesh));
  EXPECT_NEAR(pAssemblyTwo.system.rhs()[0], 0.0, 1e-12);
  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto pResultTwo = solver.solve(pAssemblyTwo.system);
  ASSERT_TRUE(pResultTwo.converged());
  EXPECT_NEAR(pResultTwo.solution[0], 0.0, 1e-12);  // pinned exactly, not just near
}

// --- Full pipeline: real transient predictor through correction #2 -------

TEST(SecondPressureCorrectionTest, FullPipelineSecondCorrectionIsFiniteAndConverged) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto first = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                            velocityBoundaries, pressureBoundaries, previousU,
                                            previousV, /*dt=*/0.01, /*referenceCell=*/0);
  ASSERT_TRUE(first.momentumConverged);
  ASSERT_TRUE(first.pressureConverged);

  const auto second =
      runSecondPressureCorrectionAssembly(mesh, first, pressureBoundaries, fluid.density(), 0);
  ASSERT_TRUE(second.secondPressureConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_TRUE(std::isfinite(second.pPrimeTwo[i])) << "cell " << i;
  }
}

TEST(SecondPressureCorrectionTest, RealPipelineTightFirstCorrectionGivesNearZeroSecondCorrection) {
  // With a tightly-converged correction #1 (the only kind PISO-E ever
  // produces), Rc1 is already at solver-tolerance-zero everywhere
  // (verified: max |Rc1| ~1e-15 on this exact 4x4 cavity), so a real,
  // non-synthetic p'2 should itself be at that same noise floor -- this
  // is the real-pipeline analogue of the hand-derived
  // FullFirstCorrectionGivesExactlyZeroSecondCorrection probe above.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto first = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                            velocityBoundaries, pressureBoundaries, previousU,
                                            previousV, /*dt=*/0.01, /*referenceCell=*/0);
  ASSERT_TRUE(first.momentumConverged);
  ASSERT_TRUE(first.pressureConverged);

  const auto second =
      runSecondPressureCorrectionAssembly(mesh, first, pressureBoundaries, fluid.density(), 0);
  ASSERT_TRUE(second.secondPressureConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(second.pPrimeTwo[i], 0.0, 1e-6) << "cell " << i;
  }
}

TEST(SecondPressureCorrectionTest, SameResponseCoefficientsReusedNoMomentumReassembly) {
  // No second call to assembleTransientMomentumComponent happens between
  // the two correctors (structurally true by construction of
  // runSecondPressureCorrectionAssembly, which only ever receives
  // first.dU/first.dV) -- this test proves the *consequence*: the
  // second assembly's faceCoefficient is identical to the first's,
  // cell-by-cell, which could only happen if the same d was used, since
  // faceCoefficient is a deterministic function of d/mesh/density alone.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto first = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                            velocityBoundaries, pressureBoundaries, previousU,
                                            previousV, /*dt=*/0.01, /*referenceCell=*/0);
  ASSERT_TRUE(first.momentumConverged);
  ASSERT_TRUE(first.pressureConverged);

  const auto second =
      runSecondPressureCorrectionAssembly(mesh, first, pressureBoundaries, fluid.density(), 0);
  ASSERT_TRUE(second.secondPressureConverged);

  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_DOUBLE_EQ(second.pAssemblyTwo->faceCoefficient[i],
                     first.pAssemblyOne->faceCoefficient[i])
        << "face " << i;
  }
}

TEST(SecondPressureCorrectionTest, FirstCorrectionStateAndPreviousStateRemainUntouched) {
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

  auto first = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid, velocityBoundaries,
                                      pressureBoundaries, previousU, previousV, /*dt=*/0.01,
                                      /*referenceCell=*/0);
  ASSERT_TRUE(first.momentumConverged);
  ASSERT_TRUE(first.pressureConverged);
  const SurfaceField correctedFluxCopy = first.correctedFlux;
  const ScalarField correctedPressureCopy = first.correctedPressure;

  const auto second =
      runSecondPressureCorrectionAssembly(mesh, first, pressureBoundaries, fluid.density(), 0);
  ASSERT_TRUE(second.secondPressureConverged);

  for (Index i = 0; i < previousU.size(); ++i) {
    EXPECT_DOUBLE_EQ(previousU[i], previousUCopy[i]);
    EXPECT_DOUBLE_EQ(previousV[i], previousVCopy[i]);
  }
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(first.correctedPressure[i], correctedPressureCopy[i]);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_DOUBLE_EQ(first.correctedFlux[i], correctedFluxCopy[i]);
  }
}

TEST(SecondPressureCorrectionTest, RepeatedSecondCorrectionAssemblyIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto first = runFirstCorrectionStep(mesh, velocity, pressure, massFlux, fluid,
                                            velocityBoundaries, pressureBoundaries, previousU,
                                            previousV, /*dt=*/0.01, /*referenceCell=*/0);
  ASSERT_TRUE(first.momentumConverged);
  ASSERT_TRUE(first.pressureConverged);

  const auto secondA =
      runSecondPressureCorrectionAssembly(mesh, first, pressureBoundaries, fluid.density(), 0);
  const auto secondB =
      runSecondPressureCorrectionAssembly(mesh, first, pressureBoundaries, fluid.density(), 0);
  ASSERT_TRUE(secondA.secondPressureConverged);
  ASSERT_TRUE(secondB.secondPressureConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(secondA.pPrimeTwo[i], secondB.pPrimeTwo[i]);
  }
}
