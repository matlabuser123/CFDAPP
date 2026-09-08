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
using cfd::physics::ContinuityResult;
using cfd::physics::evaluateContinuity;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::assembleTransientMomentumComponent;
using cfd::pressure_velocity::computeMomentumResponseCoefficient;
using cfd::pressure_velocity::correctFaceMassFlux;
using cfd::pressure_velocity::correctVelocity;
using cfd::pressure_velocity::PressureCorrectionAssembly;

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

// One full PISO timestep's worth of correction machinery -- predictor ->
// correction #1 (assemble/solve/apply) -> correction #2 (assemble/solve/
// apply) -> continuity at all three stages -> STOP. Reproduced locally
// per this test suite's own established precedent (PISO-D/E/F) of not
// sharing helpers across files. Still not production API; that's PISO-H.
//
// `firstCorrectionFraction` lets a caller apply less than the full
// solved p'1 for correction #1 (1.0 = the normal, full PISO-E behavior).
// This is a legitimate probe technique, not new production functionality
// -- the pressure-correction system is linear, so applying a fraction t
// of p'1 and then solving+applying a second correction from the
// resulting (deliberately incomplete) F1 is a well-defined, deterministic
// way to exercise a genuinely nonzero Rc1 without depending on solver
// iteration counts (see PISO-F's own hand-derived precedent for why).
struct FinalPisoStepResult {
  VectorField predictorVelocity;
  SurfaceField predictorFlux;
  ScalarField dU;
  ScalarField dV;
  ScalarField pPrimeOne;
  ScalarField p1;
  VectorField U1;
  SurfaceField F1;
  ScalarField pPrimeTwo;
  ScalarField p2;
  VectorField U2;
  SurfaceField F2;
  ContinuityResult rcStar;
  ContinuityResult rc1;
  ContinuityResult rc2;
  bool momentumConverged = false;
  bool pressureOneConverged = false;
  bool pressureTwoConverged = false;
};

FinalPisoStepResult runFullPisoStepThroughSecondCorrection(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& pressure,
    const SurfaceField& massFlux, const FluidProperties& fluid,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& pressureBoundaries,
    const ScalarField& previousU, const ScalarField& previousV, Real dt, Index referenceCell,
    Real firstCorrectionFraction = 1.0) {
  const auto uAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);
  const auto vAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::V, previousV, dt);

  const BiCGSTAB solver = makeProbeSolver();
  const auto uResult = solver.solve(uAssembly.system);
  const auto vResult = solver.solve(vAssembly.system);

  FinalPisoStepResult out;
  out.momentumConverged = uResult.converged() && vResult.converged();
  if (!out.momentumConverged) return out;

  out.predictorVelocity = VectorField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.predictorVelocity[cell.id()] =
        Vector2{uResult.solution[cell.id()], vResult.solution[cell.id()]};
  }
  out.predictorFlux = calculateMassFlux(mesh, out.predictorVelocity, fluid, velocityBoundaries);
  out.rcStar = evaluateContinuity(mesh, out.predictorFlux);

  out.dU = computeMomentumResponseCoefficient(mesh, uAssembly.diagonal);
  out.dV = computeMomentumResponseCoefficient(mesh, vAssembly.diagonal);
  const PressureCorrectionAssembly pAssemblyOne = assemblePressureCorrection(
      mesh, out.predictorFlux, out.dU, out.dV, fluid.density(), referenceCell, pressureBoundaries);
  const auto pResultOne = solver.solve(pAssemblyOne.system);
  out.pressureOneConverged = pResultOne.converged();
  if (!out.pressureOneConverged) return out;

  ScalarField pPrimeOneSolved(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    pPrimeOneSolved[cell.id()] = pResultOne.solution[cell.id()];
  }
  // Apply only `firstCorrectionFraction` of the solved p'1 -- 1.0 for the
  // normal full PISO-E behavior, <1.0 only as a deliberate, deterministic
  // probe technique (see comment above).
  out.pPrimeOne = pPrimeOneSolved * firstCorrectionFraction;

  out.p1 = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.p1[cell.id()] = pressure[cell.id()] + out.pPrimeOne[cell.id()];
  }
  out.U1 = correctVelocity(mesh, out.predictorVelocity, out.dU, out.dV, out.pPrimeOne,
                           pressureBoundaries);
  out.F1 =
      correctFaceMassFlux(mesh, out.predictorFlux, pAssemblyOne.faceCoefficient, out.pPrimeOne);
  out.rc1 = evaluateContinuity(mesh, out.F1);

  // Correction #2: assembled from F1 (never F*), same dU/dV -- no
  // momentum reassembly.
  const PressureCorrectionAssembly pAssemblyTwo = assemblePressureCorrection(
      mesh, out.F1, out.dU, out.dV, fluid.density(), referenceCell, pressureBoundaries);
  const auto pResultTwo = solver.solve(pAssemblyTwo.system);
  out.pressureTwoConverged = pResultTwo.converged();
  if (!out.pressureTwoConverged) return out;

  out.pPrimeTwo = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.pPrimeTwo[cell.id()] = pResultTwo.solution[cell.id()];
  }

  out.p2 = ScalarField(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    out.p2[cell.id()] = out.p1[cell.id()] + out.pPrimeTwo[cell.id()];
  }
  out.U2 = correctVelocity(mesh, out.U1, out.dU, out.dV, out.pPrimeTwo, pressureBoundaries);
  // Correct the authoritative F1 directly with the SAME faceCoefficient
  // correction #2's matrix was assembled with -- never regenerate from
  // interpolated corrected cell velocities.
  out.F2 = correctFaceMassFlux(mesh, out.F1, pAssemblyTwo.faceCoefficient, out.pPrimeTwo);
  out.rc2 = evaluateContinuity(mesh, out.F2);

  return out;
}

}  // namespace

// --- Direct application checks (no pipeline needed) -----------------------

TEST(FinalCorrectionTest, ZeroSecondCorrectionLeavesFirstCorrectionStateBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const VectorField u1(mesh.numberOfCells(), Vector2{0.7, -0.2});
  const ScalarField p1(mesh.numberOfCells(), 3.5);
  SurfaceField f1(mesh.numberOfFaces());
  for (Index i = 0; i < f1.size(); ++i) f1[i] = 0.03 * static_cast<Real>(i);
  const ScalarField dU(mesh.numberOfCells(), 0.4);
  const ScalarField dV(mesh.numberOfCells(), 0.6);
  const ScalarField pPrimeTwo(mesh.numberOfCells(), 0.0);  // p'2 = 0

  ScalarField p2(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) p2[cell.id()] = p1[cell.id()] + pPrimeTwo[cell.id()];
  const VectorField u2 = correctVelocity(mesh, u1, dU, dV, pPrimeTwo, pressureBoundaries);
  const SurfaceField faceCoefficient(mesh.numberOfFaces(), 1.0);  // irrelevant when p'2=0
  const SurfaceField f2 = correctFaceMassFlux(mesh, f1, faceCoefficient, pPrimeTwo);

  for (const auto& cell : mesh.cells()) {
    EXPECT_DOUBLE_EQ(p2[cell.id()], p1[cell.id()]);
    EXPECT_DOUBLE_EQ(u2[cell.id()].x, u1[cell.id()].x);
    EXPECT_DOUBLE_EQ(u2[cell.id()].y, u1[cell.id()].y);
  }
  for (Index i = 0; i < f1.size(); ++i) {
    EXPECT_DOUBLE_EQ(f2[i], f1[i]);
  }
}

// --- Hand-derived two-cell probes ------------------------------------------
//
// Same topology/coefficients as PISO-D/E/F: V=1 each, d=0.25,
// D_f(internal)=0.25, F*=[left=-1.0, internal=0.6, right=1.0],
// Rc*=[-0.4,+0.4], full converged p'1=[0,-1.6].

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

TEST(FinalCorrectionTest, TwoCellFullFirstCorrectionMakesSecondCorrectionExactlyIdempotent) {
  // Rc1 = 0 exactly (full p'1 applied) -> p'2 solves to exactly 0 ->
  // p2 == p1, F2 == F1 bit-exact, Rc2 == Rc1 == 0.
  const auto g = makeTwoCellProbeGeometry();
  const Real density = 1.0;
  SurfaceField predictorFlux(g.mesh.numberOfFaces(), 0.0);
  predictorFlux[g.leftFaceId] = -1.0;
  predictorFlux[g.internalFaceId] = 0.6;
  predictorFlux[g.rightFaceId] = 1.0;

  ScalarField pPrimeOne(2);
  pPrimeOne[0] = 0.0;
  pPrimeOne[1] = -1.6;
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(g.mesh);
  const auto pAssemblyOne = assemblePressureCorrection(g.mesh, predictorFlux, g.responseCoefficient,
                                                       g.responseCoefficient, density,
                                                       /*referenceCell=*/0, pressureBoundaries);
  const ScalarField p0(2, 0.0);
  ScalarField p1(2);
  for (Index i = 0; i < 2; ++i) p1[i] = p0[i] + pPrimeOne[i];
  const SurfaceField f1 =
      correctFaceMassFlux(g.mesh, predictorFlux, pAssemblyOne.faceCoefficient, pPrimeOne);
  const auto rc1 = evaluateContinuity(g.mesh, f1);
  ASSERT_NEAR(rc1.maxCellImbalance, 0.0, 1e-12);

  const auto pAssemblyTwo =
      assemblePressureCorrection(g.mesh, f1, g.responseCoefficient, g.responseCoefficient, density,
                                 /*referenceCell=*/0, pressureBoundaries);
  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto pResultTwo = solver.solve(pAssemblyTwo.system);
  ASSERT_TRUE(pResultTwo.converged());
  ScalarField pPrimeTwo(2);
  pPrimeTwo[0] = pResultTwo.solution[0];
  pPrimeTwo[1] = pResultTwo.solution[1];
  EXPECT_NEAR(pPrimeTwo[0], 0.0, 1e-9);
  EXPECT_NEAR(pPrimeTwo[1], 0.0, 1e-9);

  ScalarField p2(2);
  for (Index i = 0; i < 2; ++i) p2[i] = p1[i] + pPrimeTwo[i];
  const SurfaceField f2 = correctFaceMassFlux(g.mesh, f1, pAssemblyTwo.faceCoefficient, pPrimeTwo);
  const auto rc2 = evaluateContinuity(g.mesh, f2);

  EXPECT_NEAR(p2[0], p1[0], 1e-9);
  EXPECT_NEAR(p2[1], p1[1], 1e-9);
  EXPECT_NEAR(f2[g.internalFaceId], f1[g.internalFaceId], 1e-9);
  EXPECT_NEAR(f2[g.leftFaceId], f1[g.leftFaceId], 1e-12);    // boundary: untouched either way
  EXPECT_NEAR(f2[g.rightFaceId], f1[g.rightFaceId], 1e-12);  // boundary: untouched either way
  EXPECT_NEAR(rc2.maxCellImbalance, rc1.maxCellImbalance, 1e-9);
  EXPECT_NEAR(rc2.maxCellImbalance, 0.0, 1e-9);
}

TEST(FinalCorrectionTest, TwoCellPartialFirstCorrectionPlusSecondReconstructsFullSingleCorrection) {
  // p'1_half = [0,-0.8] (half of the full converged p'1=[0,-1.6]) ->
  // Rc1 = [-0.2,+0.2] (PISO-F) -> p'2 solves to [0,-0.8] (PISO-F) ->
  // applying BOTH in sequence must reconstruct exactly what applying the
  // single full p'1=[0,-1.6] directly would have given.
  const auto g = makeTwoCellProbeGeometry();
  const Real density = 1.0;
  SurfaceField predictorFlux(g.mesh.numberOfFaces(), 0.0);
  predictorFlux[g.leftFaceId] = -1.0;
  predictorFlux[g.internalFaceId] = 0.6;
  predictorFlux[g.rightFaceId] = 1.0;
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(g.mesh);
  const ScalarField p0(2, 0.0);

  // Chain: half correction #1, then correction #2.
  ScalarField pPrimeOneHalf(2);
  pPrimeOneHalf[0] = 0.0;
  pPrimeOneHalf[1] = -0.8;
  const auto pAssemblyOne = assemblePressureCorrection(g.mesh, predictorFlux, g.responseCoefficient,
                                                       g.responseCoefficient, density,
                                                       /*referenceCell=*/0, pressureBoundaries);
  ScalarField p1Half(2);
  for (Index i = 0; i < 2; ++i) p1Half[i] = p0[i] + pPrimeOneHalf[i];
  const SurfaceField f1Half =
      correctFaceMassFlux(g.mesh, predictorFlux, pAssemblyOne.faceCoefficient, pPrimeOneHalf);

  const auto pAssemblyTwo =
      assemblePressureCorrection(g.mesh, f1Half, g.responseCoefficient, g.responseCoefficient,
                                 density, /*referenceCell=*/0, pressureBoundaries);
  const BiCGSTAB solver(LinearSolverSettings{1e-14, 1e-12, 100});
  const auto pResultTwo = solver.solve(pAssemblyTwo.system);
  ASSERT_TRUE(pResultTwo.converged());
  ScalarField pPrimeTwo(2);
  pPrimeTwo[0] = pResultTwo.solution[0];
  pPrimeTwo[1] = pResultTwo.solution[1];
  ScalarField p2Chain(2);
  for (Index i = 0; i < 2; ++i) p2Chain[i] = p1Half[i] + pPrimeTwo[i];
  const SurfaceField f2Chain =
      correctFaceMassFlux(g.mesh, f1Half, pAssemblyTwo.faceCoefficient, pPrimeTwo);

  // Single full correction, applied once.
  ScalarField pPrimeFull(2);
  pPrimeFull[0] = 0.0;
  pPrimeFull[1] = -1.6;
  ScalarField pFullOnce(2);
  for (Index i = 0; i < 2; ++i) pFullOnce[i] = p0[i] + pPrimeFull[i];
  const SurfaceField fFullOnce =
      correctFaceMassFlux(g.mesh, predictorFlux, pAssemblyOne.faceCoefficient, pPrimeFull);

  EXPECT_NEAR(p2Chain[0], pFullOnce[0], 1e-9);
  EXPECT_NEAR(p2Chain[1], pFullOnce[1], 1e-9);
  EXPECT_NEAR(f2Chain[g.internalFaceId], fFullOnce[g.internalFaceId], 1e-9);
  EXPECT_NEAR(f2Chain[g.leftFaceId], fFullOnce[g.leftFaceId], 1e-12);
  EXPECT_NEAR(f2Chain[g.rightFaceId], fFullOnce[g.rightFaceId], 1e-12);
}

// --- Full pipeline: real predictor through correction #2 -------------------

TEST(FinalCorrectionTest, FullPipelineFinalStateIsFiniteAndConverged) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureOneConverged);
  ASSERT_TRUE(out.pressureTwoConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_TRUE(std::isfinite(out.p2[i])) << "cell " << i;
    EXPECT_TRUE(std::isfinite(out.U2[i].x)) << "cell " << i;
    EXPECT_TRUE(std::isfinite(out.U2[i].y)) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_TRUE(std::isfinite(out.F2[i])) << "face " << i;
  }
  EXPECT_TRUE(std::isfinite(out.rc2.globalNetFlux));
  EXPECT_TRUE(std::isfinite(out.rc2.totalAbsoluteImbalance));
  EXPECT_TRUE(std::isfinite(out.rc2.maxCellImbalance));
}

TEST(FinalCorrectionTest, WallNormalBoundaryFluxStaysExactlyZeroInFinalState) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureOneConverged);
  ASSERT_TRUE(out.pressureTwoConverged);
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(out.F2[faceId], 0.0, 1e-9) << "boundary face " << faceId;
    }
  }
}

TEST(FinalCorrectionTest, FinalContinuityIsEvaluatedFromF2NotF1OrFStar) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  // Half-applied first correction, so F2 is known to differ substantially
  // from F1 -- lets this test actually distinguish "rc2 recomputed from
  // F2" from "rc2 accidentally left over as rc1's value".
  const auto out = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0, /*firstCorrectionFraction=*/0.5);
  ASSERT_TRUE(out.pressureTwoConverged);

  const auto directFromF2 = evaluateContinuity(mesh, out.F2);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(out.rc2.cellImbalance[i], directFromF2.cellImbalance[i]);
  }
  EXPECT_DOUBLE_EQ(out.rc2.globalNetFlux, directFromF2.globalNetFlux);
  // rc2 must not merely equal rc1 (i.e. it was genuinely recomputed from
  // F2, not left over from correction #1's stage).
  EXPECT_NE(out.rc2.maxCellImbalance, out.rc1.maxCellImbalance);
}

TEST(FinalCorrectionTest, ControlledPartialCorrectionShowsRc2BelowRc1) {
  // "controlled partial correction: Rc2 < Rc1" -- half-applying p'1
  // deliberately leaves a genuinely nonzero Rc1 (not solver noise, see
  // PISO-F), and correction #2 must reduce it.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0, /*firstCorrectionFraction=*/0.5);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureOneConverged);
  ASSERT_TRUE(out.pressureTwoConverged);
  EXPECT_GT(out.rc1.maxCellImbalance, 1e-4);  // genuinely nonzero, not noise
  EXPECT_LT(out.rc2.maxCellImbalance, out.rc1.maxCellImbalance);
  EXPECT_NEAR(out.rc2.maxCellImbalance, 0.0, 1e-6);
}

TEST(FinalCorrectionTest, RealPipelineSecondCorrectionIsEffectivelyIdempotentAfterFullFirst) {
  // "already-converged correction: correction #2 is effectively
  // idempotent" -- with the FULL (not partial) correction #1, Rc1 is
  // already at solver-noise level, so p2/U2/F2 should be close to
  // p1/U1/F1, not necessarily with Rc2 < Rc1 (explicitly not required
  // here -- correction #1 may already be as good as floating point
  // allows, per PISO-F's RealPipelineTightFirstCorrectionGivesNearZero
  // SecondCorrection finding).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto out = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0, /*firstCorrectionFraction=*/1.0);

  ASSERT_TRUE(out.momentumConverged);
  ASSERT_TRUE(out.pressureOneConverged);
  ASSERT_TRUE(out.pressureTwoConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(out.p2[i], out.p1[i], 1e-5) << "cell " << i;
    EXPECT_NEAR(out.U2[i].x, out.U1[i].x, 1e-5) << "cell " << i;
    EXPECT_NEAR(out.U2[i].y, out.U1[i].y, 1e-5) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_NEAR(out.F2[i], out.F1[i], 1e-5) << "face " << i;
  }
}

TEST(FinalCorrectionTest,
     PartialFirstCorrectionPlusSecondReconstructsFullSingleCorrectionRealMesh) {
  // The general chain-reconstruction property proven by hand on the
  // 2-cell probe (TwoCellPartialFirstCorrectionPlusSecondReconstructs
  // FullSingleCorrection) also holds on a real, non-trivial mesh: the
  // pressure-correction equation is linear, so p'1(t) + p'2(t) = p'1_full
  // for any fraction t (not just t=0.5) applied on the first correction,
  // given the same reference cell and same dU/dV are used throughout.
  // Compare pressure, velocity, AND authoritative face flux -- not
  // pressure alone.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto chain = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0, /*firstCorrectionFraction=*/0.5);
  ASSERT_TRUE(chain.momentumConverged);
  ASSERT_TRUE(chain.pressureOneConverged);
  ASSERT_TRUE(chain.pressureTwoConverged);

  // Single full correction #1, applied once, no second correction.
  const auto single = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0, /*firstCorrectionFraction=*/1.0);
  ASSERT_TRUE(single.momentumConverged);
  ASSERT_TRUE(single.pressureOneConverged);

  const Real tol = 1e-6;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(chain.p2[i], single.p1[i], tol) << "cell " << i;
    EXPECT_NEAR(chain.U2[i].x, single.U1[i].x, tol) << "cell " << i;
    EXPECT_NEAR(chain.U2[i].y, single.U1[i].y, tol) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_NEAR(chain.F2[i], single.F1[i], tol) << "face " << i;
  }
}

TEST(FinalCorrectionTest,
     PreviousStateRemainsUntouchedAndFirstCorrectionStateIsPreservedSeparately) {
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

  // Half-applied first correction, so out.p1/out.U1/out.F1 are genuinely
  // distinct from the predictor AND from out.p2/out.U2/out.F2 -- makes
  // this test able to actually distinguish "preserved separately" from
  // "accidentally aliased/overwritten", unlike the fraction=1.0 case
  // where correction #2 is itself near-idempotent.
  const auto out = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0, /*firstCorrectionFraction=*/0.5);
  ASSERT_TRUE(out.pressureTwoConverged);

  for (Index i = 0; i < previousU.size(); ++i) {
    EXPECT_DOUBLE_EQ(previousU[i], previousUCopy[i]);
    EXPECT_DOUBLE_EQ(previousV[i], previousVCopy[i]);
  }
  // out.p1/out.U1/out.F1 (the accepted correction-#1 state) are distinct
  // storage from out.p2/out.U2/out.F2 -- applying correction #2 did not
  // silently overwrite them in place; with a half-applied first
  // correction the two stages are known to differ substantially.
  bool anyPressureDiffers = false;
  bool anyFluxDiffers = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (out.p1[i] != out.p2[i]) anyPressureDiffers = true;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    if (out.F1[i] != out.F2[i]) anyFluxDiffers = true;
  }
  EXPECT_TRUE(anyPressureDiffers);
  EXPECT_TRUE(anyFluxDiffers);
}

TEST(FinalCorrectionTest, RepeatedFullPipelineIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);

  const auto outA = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);
  const auto outB = runFullPisoStepThroughSecondCorrection(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries, previousU,
      previousV, /*dt=*/0.01, /*referenceCell=*/0);

  ASSERT_TRUE(outA.pressureTwoConverged);
  ASSERT_TRUE(outB.pressureTwoConverged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(outA.p2[i], outB.p2[i]);
    EXPECT_EQ(outA.U2[i].x, outB.U2[i].x);
    EXPECT_EQ(outA.U2[i].y, outB.U2[i].y);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(outA.F2[i], outB.F2[i]);
  }
  EXPECT_EQ(outA.rc2.globalNetFlux, outB.rc2.globalNetFlux);
}
