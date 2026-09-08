#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
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
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"
#include "cfd/solver/TransientSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
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
using cfd::pressure_velocity::PISO;
using cfd::pressure_velocity::PISOSettings;
using cfd::solver::TransientState;
using cfd::solver::TransientStepStatus;

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

// Same solver settings PISO-D/E/F/G's own test-local pipelines used --
// reused here so PISO::solveTimeStep and the independent manual chain
// below solve bit-identical linear systems (same matrices, same solver
// settings), making a bit-identical comparison meaningful rather than
// "close to within solver tolerance".
PISOSettings makeProbeSettings() {
  LinearSolverSettings linear;
  linear.maxIterations = 500;
  linear.absoluteTolerance = 1e-12;
  linear.relativeTolerance = 1e-10;
  PISOSettings settings;
  settings.momentumSolver = linear;
  settings.pressureSolver = linear;
  return settings;
}

// Independent, from-scratch re-derivation of exactly what PISO::
// solveTimeStep is documented to do -- predictor -> correction #1
// (assemble/solve/apply) -> correction #2 (assemble from F1, not F* /
// solve/apply) -> final continuity from F2 -- built without reusing any
// of PISO.cpp's own code, so that comparing its output against
// PISO::solveTimeStep's is a genuine cross-check, not a tautology.
// Mirrors PISO-D through PISO-G's own established test-local pipeline
// pattern in this same directory.
struct ManualPisoStepResult {
  TransientStepStatus status = TransientStepStatus::InvalidConfiguration;
  TransientState state;
  Real continuityResidual = 0.0;
  Real massImbalance = 0.0;
};

ManualPisoStepResult runManualPisoStep(const Mesh& mesh, const TransientState& previousState,
                                       const FluidProperties& fluid,
                                       const BoundaryConditionSet& velocityBoundaries,
                                       const BoundaryConditionSet& pressureBoundaries, Real dt,
                                       Index referenceCell) {
  const BiCGSTAB solver(makeProbeSettings().momentumSolver);

  ScalarField previousUCopy(mesh.numberOfCells());
  ScalarField previousVCopy(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    previousUCopy[cell.id()] = previousState.velocity[cell.id()].x;
    previousVCopy[cell.id()] = previousState.velocity[cell.id()].y;
  }

  const auto uAssembly = assembleTransientMomentumComponent(
      mesh, previousState.velocity, previousState.pressure, previousState.massFlux, fluid,
      velocityBoundaries, pressureBoundaries, VelocityComponent::U, previousUCopy, dt);
  const auto vAssembly = assembleTransientMomentumComponent(
      mesh, previousState.velocity, previousState.pressure, previousState.massFlux, fluid,
      velocityBoundaries, pressureBoundaries, VelocityComponent::V, previousVCopy, dt);

  const auto uResult = solver.solve(uAssembly.system);
  const auto vResult = solver.solve(vAssembly.system);

  ManualPisoStepResult out;
  if (!uResult.converged() || !vResult.converged()) {
    out.status = TransientStepStatus::MomentumFailure;
    out.state = previousState;
    return out;
  }

  VectorField predictorVelocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    predictorVelocity[cell.id()] =
        Vector2{uResult.solution[cell.id()], vResult.solution[cell.id()]};
  }
  const SurfaceField predictorFlux =
      calculateMassFlux(mesh, predictorVelocity, fluid, velocityBoundaries);

  const ScalarField dU = computeMomentumResponseCoefficient(mesh, uAssembly.diagonal);
  const ScalarField dV = computeMomentumResponseCoefficient(mesh, vAssembly.diagonal);

  const auto pAssemblyOne = assemblePressureCorrection(mesh, predictorFlux, dU, dV, fluid.density(),
                                                       referenceCell, pressureBoundaries);
  const auto pResultOne = solver.solve(pAssemblyOne.system);
  if (!pResultOne.converged()) {
    out.status = TransientStepStatus::PressureCorrectionFailure;
    out.state = previousState;
    return out;
  }
  ScalarField pPrimeOne(mesh.numberOfCells());
  for (Index i = 0; i < pPrimeOne.size(); ++i) pPrimeOne[i] = pResultOne.solution[i];

  ScalarField p1(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    p1[cell.id()] = previousState.pressure[cell.id()] + pPrimeOne[cell.id()];
  }
  const VectorField u1 =
      correctVelocity(mesh, predictorVelocity, dU, dV, pPrimeOne, pressureBoundaries);
  const SurfaceField f1 =
      correctFaceMassFlux(mesh, predictorFlux, pAssemblyOne.faceCoefficient, pPrimeOne);

  const auto pAssemblyTwo = assemblePressureCorrection(mesh, f1, dU, dV, fluid.density(),
                                                       referenceCell, pressureBoundaries);
  const auto pResultTwo = solver.solve(pAssemblyTwo.system);
  if (!pResultTwo.converged()) {
    out.status = TransientStepStatus::PressureCorrectionFailure;
    out.state = previousState;
    return out;
  }
  ScalarField pPrimeTwo(mesh.numberOfCells());
  for (Index i = 0; i < pPrimeTwo.size(); ++i) pPrimeTwo[i] = pResultTwo.solution[i];

  ScalarField p2(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) p2[cell.id()] = p1[cell.id()] + pPrimeTwo[cell.id()];
  const VectorField u2 = correctVelocity(mesh, u1, dU, dV, pPrimeTwo, pressureBoundaries);
  const SurfaceField f2 = correctFaceMassFlux(mesh, f1, pAssemblyTwo.faceCoefficient, pPrimeTwo);

  const auto finalContinuity = evaluateContinuity(mesh, f2);

  out.status = TransientStepStatus::Converged;
  out.state.velocity = u2;
  out.state.pressure = p2;
  out.state.massFlux = f2;
  out.continuityResidual = finalContinuity.maxCellImbalance;
  out.massImbalance = std::abs(finalContinuity.globalNetFlux);
  return out;
}

}  // namespace

// --- The strongest regression: PISO::solveTimeStep == manual A->G chain ---

TEST(PisoTest, OneStepMatchesIndependentManualPipelineBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const Real dt = 0.01;
  const Index referenceCell = 0;

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(),
                  referenceCell);
  const auto pisoResult = piso.solveTimeStep(previousState, dt);
  const auto manual = runManualPisoStep(mesh, previousState, fluid, velocityBoundaries,
                                        pressureBoundaries, dt, referenceCell);

  ASSERT_EQ(pisoResult.status, TransientStepStatus::Converged);
  ASSERT_EQ(manual.status, TransientStepStatus::Converged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(pisoResult.state.pressure[i], manual.state.pressure[i]) << "cell " << i;
    EXPECT_EQ(pisoResult.state.velocity[i].x, manual.state.velocity[i].x) << "cell " << i;
    EXPECT_EQ(pisoResult.state.velocity[i].y, manual.state.velocity[i].y) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(pisoResult.state.massFlux[i], manual.state.massFlux[i]) << "face " << i;
  }
  EXPECT_EQ(pisoResult.continuityResidual, manual.continuityResidual);
  EXPECT_EQ(pisoResult.massImbalance, manual.massImbalance);
}

TEST(PisoTest, PreviousStateArgumentIsNotMutated) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);
  const VectorField velocityCopy = previousState.velocity;
  const ScalarField pressureCopy = previousState.pressure;
  const SurfaceField massFluxCopy = previousState.massFlux;

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);
  ASSERT_EQ(result.status, TransientStepStatus::Converged);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(previousState.velocity[i].x, velocityCopy[i].x);
    EXPECT_DOUBLE_EQ(previousState.velocity[i].y, velocityCopy[i].y);
    EXPECT_DOUBLE_EQ(previousState.pressure[i], pressureCopy[i]);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_DOUBLE_EQ(previousState.massFlux[i], massFluxCopy[i]);
  }
}

TEST(PisoTest, RepeatedIdenticalTimestepIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto resultA = piso.solveTimeStep(previousState, 0.01);
  const auto resultB = piso.solveTimeStep(previousState, 0.01);

  ASSERT_EQ(resultA.status, TransientStepStatus::Converged);
  ASSERT_EQ(resultB.status, TransientStepStatus::Converged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(resultA.state.pressure[i], resultB.state.pressure[i]);
    EXPECT_EQ(resultA.state.velocity[i].x, resultB.state.velocity[i].x);
    EXPECT_EQ(resultA.state.velocity[i].y, resultB.state.velocity[i].y);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(resultA.state.massFlux[i], resultB.state.massFlux[i]);
  }
  EXPECT_EQ(resultA.continuityResidual, resultB.continuityResidual);
  EXPECT_EQ(resultA.massImbalance, resultB.massImbalance);
  EXPECT_EQ(resultA.maxCFL, resultB.maxCFL);
}

TEST(PisoTest, OneStepResultIsFiniteOnCavity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);

  ASSERT_EQ(result.status, TransientStepStatus::Converged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_TRUE(std::isfinite(result.state.pressure[i])) << "cell " << i;
    EXPECT_TRUE(std::isfinite(result.state.velocity[i].x)) << "cell " << i;
    EXPECT_TRUE(std::isfinite(result.state.velocity[i].y)) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_TRUE(std::isfinite(result.state.massFlux[i])) << "face " << i;
  }
  EXPECT_TRUE(std::isfinite(result.continuityResidual));
  EXPECT_TRUE(std::isfinite(result.massImbalance));
  EXPECT_TRUE(std::isfinite(result.maxCFL));
}

TEST(PisoTest, WallNormalBoundaryFluxStaysExactlyZero) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);
  ASSERT_EQ(result.status, TransientStepStatus::Converged);

  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(result.state.massFlux[faceId], 0.0, 1e-9) << "boundary face " << faceId;
    }
  }
}

// --- Small controlled meshes: conservation --------------------------------

TEST(PisoTest, TwoCellClosedDomainAtRestStaysAtRestAndConservesMassExactly) {
  // A fully closed 2-cell box (Wall on every side, no MovingWall) has no
  // forcing at all -- PISO must not spuriously inject flow or mass where
  // none should exist. Mirrors the D/E/F/G hand-probe topology
  // (2 unit cells), now driven end-to-end through the real class.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "right", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);

  ASSERT_EQ(result.status, TransientStepStatus::Converged);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(result.state.pressure[i], 0.0, 1e-9) << "cell " << i;
    EXPECT_NEAR(result.state.velocity[i].x, 0.0, 1e-9) << "cell " << i;
    EXPECT_NEAR(result.state.velocity[i].y, 0.0, 1e-9) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_NEAR(result.state.massFlux[i], 0.0, 1e-9) << "face " << i;
  }
  EXPECT_NEAR(result.massImbalance, 0.0, 1e-9);
  EXPECT_NEAR(result.continuityResidual, 0.0, 1e-9);
}

TEST(PisoTest, TinyLidDrivenCavityOneStepConservesMassNearMachineZero) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto result = piso.solveTimeStep(previousState, 0.005);

  ASSERT_EQ(result.status, TransientStepStatus::Converged);
  EXPECT_NEAR(result.massImbalance, 0.0, 1e-9);
  EXPECT_NEAR(result.continuityResidual, 0.0, 1e-9);
}

// --- Configuration / input validation --------------------------------------

TEST(PisoTest, NonPositiveOrNonFiniteDtReturnsInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);

  for (const Real badDt : {0.0, -0.01, std::numeric_limits<Real>::quiet_NaN(),
                           std::numeric_limits<Real>::infinity()}) {
    const auto result = piso.solveTimeStep(previousState, badDt);
    EXPECT_EQ(result.status, TransientStepStatus::InvalidConfiguration) << "dt=" << badDt;
  }
}

TEST(PisoTest, MismatchedPreviousStateSizeReturnsInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells() - 1, Vector2{0.0, 0.0});  // wrong size
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux = SurfaceField(mesh.numberOfFaces(), 0.0);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);
  EXPECT_EQ(result.status, TransientStepStatus::InvalidConfiguration);
}

TEST(PisoTest, ReferenceCellOutOfRangeReturnsInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(),
                  mesh.numberOfCells());  // out of range
  const auto result = piso.solveTimeStep(previousState, 0.01);
  EXPECT_EQ(result.status, TransientStepStatus::InvalidConfiguration);
}

TEST(PisoTest, NonFiniteInputStateReturnsNonFiniteState) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.velocity[0] = Vector2{std::numeric_limits<Real>::quiet_NaN(), 0.0};
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux = SurfaceField(mesh.numberOfFaces(), 0.0);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);
  EXPECT_EQ(result.status, TransientStepStatus::NonFiniteState);
}

// --- Forced failure propagation --------------------------------------------

TEST(PisoTest, ForcedMomentumFailurePropagatesAsMomentumFailure) {
  // Mirrors SIMPLEFailureTest.MomentumSolverTooFewIterationsReports
  // MomentumFailure exactly: an intentionally impossible inner momentum
  // solve must not let PISO continue as though the predictor were valid.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  PISOSettings settings;
  settings.momentumSolver.maxIterations = 1;
  settings.momentumSolver.absoluteTolerance = 1e-14;
  settings.momentumSolver.relativeTolerance = 1e-14;
  settings.pressureSolver = makeProbeSettings().pressureSolver;

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, settings, 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);

  ASSERT_EQ(result.status, TransientStepStatus::MomentumFailure);
  // A failed step's state is a deterministic copy of previousState, not a
  // default-constructed empty one -- TransientSolver's own contract
  // relies on being able to safely ignore it, but this class documents a
  // stronger guarantee.
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(result.state.pressure[i], previousState.pressure[i]);
    EXPECT_DOUBLE_EQ(result.state.velocity[i].x, previousState.velocity[i].x);
    EXPECT_DOUBLE_EQ(result.state.velocity[i].y, previousState.velocity[i].y);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_DOUBLE_EQ(result.state.massFlux[i], previousState.massFlux[i]);
  }
}

TEST(PisoTest, ForcedPressureCorrectionFailurePropagatesAsPressureCorrectionFailure) {
  // Mirrors SIMPLEFailureTest.PressureSolverTooFewIterationsReports
  // PressureCorrectionFailure. This necessarily exercises correction #1's
  // failure check (the first pressure solve PISO attempts): correction
  // #2 reuses the *exact same* coefficient matrix as correction #1 (same
  // dU/dV/mesh/density -- only the RHS differs, per PISO-F/G's own
  // finding), and BiCGSTAB's iterations-to-converge for a fixed relative
  // tolerance is essentially independent of RHS scale for an identical
  // matrix -- so an "#1 succeeds under this budget but #2 fails under the
  // same budget" scenario is not a meaningfully distinct, non-fragile
  // case to construct here. Both failure branches (correction #1's and
  // correction #2's) are structurally identical code
  // (`if (!pResult.converged()) return failureResult(
  // PressureCorrectionFailure, previousState);`) in PISO.cpp, so this one
  // forced case is the genuine regression for that shared pattern.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  TransientState previousState;
  previousState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  previousState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  previousState.massFlux =
      calculateMassFlux(mesh, previousState.velocity, fluid, velocityBoundaries);

  PISOSettings settings = makeProbeSettings();
  settings.pressureSolver.maxIterations = 1;
  settings.pressureSolver.absoluteTolerance = 1e-14;
  settings.pressureSolver.relativeTolerance = 1e-14;

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, settings, 0);
  const auto result = piso.solveTimeStep(previousState, 0.01);

  ASSERT_EQ(result.status, TransientStepStatus::PressureCorrectionFailure);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(result.state.pressure[i], previousState.pressure[i]);
  }
}

// --- Accessors --------------------------------------------------------------

TEST(PisoTest, AccessorsReturnConstructedValues) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const PISOSettings settings = makeProbeSettings();

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, settings, 3);
  EXPECT_EQ(piso.referenceCell(), 3u);
  EXPECT_EQ(piso.settings().momentumSolver.maxIterations, settings.momentumSolver.maxIterations);
  EXPECT_EQ(piso.settings().pressureSolver.maxIterations, settings.pressureSolver.maxIterations);
}
