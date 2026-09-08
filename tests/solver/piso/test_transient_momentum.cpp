#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::SparseMatrixBuilder;
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
using cfd::physics::assembleConvectionContribution;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::assemblePressureSourceContribution;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::applyTransientTerm;
using cfd::pressure_velocity::assembleTransientMomentumComponent;

namespace {

SparseMatrixBuilder makeSampleBuilder() {
  // Same shape as MomentumPredictorRelaxationTest's probe
  // (test_momentum_predictor.cpp): aP=4/5 diagonal, aN=-1 off-diagonal,
  // matching the diffusion sign convention -- purely the algebra under
  // test here, not derived from any mesh/physics.
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, -1.0);
  builder.add(1, 1, 5.0);
  builder.add(1, 0, -1.0);
  return builder;
}

}  // namespace

// --- applyTransientTerm: pure algebra, no mesh -----------------------------

TEST(TransientMomentumTermTest, DiagonalGetsTimeCoefficientAddedExactly) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector diagonalContribution{3.0, 7.0};
  const Vector sourceContribution{0.0, 0.0};

  applyTransientTerm(builder, rhs, diagonalContribution, sourceContribution);
  const auto matrix = builder.build();

  EXPECT_NEAR(matrix.diagonal(0), 4.0 + 3.0, 1e-12);
  EXPECT_NEAR(matrix.diagonal(1), 5.0 + 7.0, 1e-12);
}

TEST(TransientMomentumTermTest, RhsGetsSourceContributionAddedExactly) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector diagonalContribution{3.0, 7.0};
  const Vector sourceContribution{30.0, 70.0};  // e.g. aP,time * phiOld

  applyTransientTerm(builder, rhs, diagonalContribution, sourceContribution);

  EXPECT_NEAR(rhs[0], 10.0 + 30.0, 1e-12);
  EXPECT_NEAR(rhs[1], 20.0 + 70.0, 1e-12);
}

TEST(TransientMomentumTermTest, OffDiagonalCoefficientsAreUnchanged) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector diagonalContribution{3.0, 7.0};
  const Vector sourceContribution{0.0, 0.0};

  applyTransientTerm(builder, rhs, diagonalContribution, sourceContribution);
  const auto matrix = builder.build();

  Vector e1(2, 0.0);
  e1[1] = 1.0;
  Vector e0(2, 0.0);
  e0[0] = 1.0;
  EXPECT_NEAR(matrix.multiply(e1)[0], -1.0, 1e-12);  // A(0,1) untouched
  EXPECT_NEAR(matrix.multiply(e0)[1], -1.0, 1e-12);  // A(1,0) untouched
}

TEST(TransientMomentumTermTest, RejectsSizeMismatch) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector wrongSize{1.0};
  EXPECT_THROW(applyTransientTerm(builder, rhs, wrongSize, Vector{0.0, 0.0}), InvalidArgumentError);
  EXPECT_THROW(applyTransientTerm(builder, rhs, Vector{0.0, 0.0}, wrongSize), InvalidArgumentError);
}

// --- assembleTransientMomentumComponent: mesh-based ------------------------

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

}  // namespace

TEST(TransientMomentumAssemblyTest, PreviousComponentValueIsNotMutated) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousUCopy = previousU;

  const auto assembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, /*dt=*/0.01);
  static_cast<void>(assembly);

  for (Index i = 0; i < previousU.size(); ++i) {
    EXPECT_DOUBLE_EQ(previousU[i], previousUCopy[i]);
  }
}

TEST(TransientMomentumAssemblyTest, DiagonalIncludesExactTransientCoefficient) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const Real dt = 0.01;

  // Base physics only (no transient term), assembled by hand the same
  // way assembleTransientMomentumComponent's own implementation does
  // internally -- an independent reference to compare its output
  // against, not a re-test of assembleDiffusionContribution/
  // assembleConvectionContribution themselves (those already have their
  // own tests).
  SparseMatrixBuilder baseBuilder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector baseRhs(mesh.numberOfCells(), 0.0);
  assembleDiffusionContribution(mesh, fluid.dynamicViscosity(), velocity, velocityBoundaries,
                                VelocityComponent::U, baseBuilder, baseRhs);
  assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, VelocityComponent::U,
                                 baseBuilder, baseRhs);
  const auto baseMatrix = baseBuilder.build();

  const auto transientAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);

  for (const auto& cell : mesh.cells()) {
    const Real expectedAPTime = fluid.density() * cell.volume() / dt;
    EXPECT_NEAR(transientAssembly.diagonal[cell.id()],
                baseMatrix.diagonal(cell.id()) + expectedAPTime, 1e-9)
        << "at cell " << cell.id();
  }
}

TEST(TransientMomentumAssemblyTest, RejectsVelocityPressureSizeMismatch) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField wrongVelocity(mesh.numberOfCells() - 1, Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);

  EXPECT_THROW((void)assembleTransientMomentumComponent(
                   mesh, wrongVelocity, pressure, massFlux, fluid, velocityBoundaries,
                   pressureBoundaries, VelocityComponent::U, previousU, 0.01),
               InvalidArgumentError);
}

TEST(TransientMomentumAssemblyTest, RejectsMassFluxSizeMismatch) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField wrongMassFlux(mesh.numberOfFaces() - 1, 0.0);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);

  EXPECT_THROW((void)assembleTransientMomentumComponent(
                   mesh, velocity, pressure, wrongMassFlux, fluid, velocityBoundaries,
                   pressureBoundaries, VelocityComponent::U, previousU, 0.01),
               InvalidArgumentError);
}

TEST(TransientMomentumAssemblyTest, RejectsNonPositiveOrNonFiniteDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);

  EXPECT_THROW((void)assembleTransientMomentumComponent(mesh, velocity, pressure, massFlux, fluid,
                                                        velocityBoundaries, pressureBoundaries,
                                                        VelocityComponent::U, previousU, 0.0),
               InvalidArgumentError);
  EXPECT_THROW((void)assembleTransientMomentumComponent(mesh, velocity, pressure, massFlux, fluid,
                                                        velocityBoundaries, pressureBoundaries,
                                                        VelocityComponent::U, previousU, -0.01),
               InvalidArgumentError);
}

// --- Predictor probe: a tiny impulsively-started cavity ---------------------
//
// The physical scenario TODO.md P2 section 45 eventually validates fully
// (impulsively started lid-driven cavity) -- here just the *first*
// predictor step from rest, independent of any pressure correction or
// PISO class (neither exists yet). Solves u*/v* from the transient
// momentum equation, then constructs the predictor face flux with the
// same authoritative cfd::physics::calculateMassFlux SIMPLE already uses
// -- "Do not introduce an independent PISO flux convention."

namespace {

BiCGSTAB makeProbeSolver() {
  LinearSolverSettings settings;
  settings.maxIterations = 500;
  settings.absoluteTolerance = 1e-10;
  settings.relativeTolerance = 1e-8;
  return BiCGSTAB(settings);
}

}  // namespace

TEST(TransientMomentumPredictorProbeTest, PredictorVelocityIsFiniteFromRest) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});  // at rest
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);
  const Real dt = 0.01;

  const auto uAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);
  const auto vAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::V, previousV, dt);

  const BiCGSTAB solver = makeProbeSolver();
  const auto uResult = solver.solve(uAssembly.system);
  const auto vResult = solver.solve(vAssembly.system);

  ASSERT_TRUE(uResult.converged());
  ASSERT_TRUE(vResult.converged());
  for (Index i = 0; i < uResult.solution.size(); ++i) {
    EXPECT_TRUE(std::isfinite(uResult.solution[i]));
    EXPECT_TRUE(std::isfinite(vResult.solution[i]));
  }
}

TEST(TransientMomentumPredictorProbeTest, PredictorFluxIsFiniteAndRespectsWallImpermeability) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const ScalarField previousV(mesh.numberOfCells(), 0.0);
  const Real dt = 0.01;

  const auto uAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);
  const auto vAssembly = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::V, previousV, dt);
  const BiCGSTAB solver = makeProbeSolver();
  const auto uResult = solver.solve(uAssembly.system);
  const auto vResult = solver.solve(vAssembly.system);
  ASSERT_TRUE(uResult.converged());
  ASSERT_TRUE(vResult.converged());

  VectorField predictorVelocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    predictorVelocity[cell.id()] =
        Vector2{uResult.solution[cell.id()], vResult.solution[cell.id()]};
  }

  // "Do not introduce an independent PISO flux convention" -- the exact
  // same authoritative calculateMassFlux SIMPLE uses for its own
  // predictor flux.
  const SurfaceField predictorFlux =
      calculateMassFlux(mesh, predictorVelocity, fluid, velocityBoundaries);

  for (Index i = 0; i < predictorFlux.size(); ++i) {
    EXPECT_TRUE(std::isfinite(predictorFlux[i])) << "face " << i;
  }
  // Every wall/moving-wall boundary face is impermeable by construction
  // (Wall/MovingWall always give exactly 0 normal velocity, independent
  // of the interior solve -- the same structural invariant
  // SIMPLEContinuityTest.BoundaryFluxRemainsNearZero rests on), so this
  // holds for a single predictor step exactly as it does for a converged
  // SIMPLE solve.
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(predictorFlux[faceId], 0.0, 1e-9) << "boundary face " << faceId;
    }
  }
}

TEST(TransientMomentumPredictorProbeTest, RepeatedPredictorSolveIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField previousU(mesh.numberOfCells(), 0.0);
  const Real dt = 0.01;
  const BiCGSTAB solver = makeProbeSolver();

  const auto assemblyA = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);
  const auto resultA = solver.solve(assemblyA.system);

  const auto assemblyB = assembleTransientMomentumComponent(
      mesh, velocity, pressure, massFlux, fluid, velocityBoundaries, pressureBoundaries,
      VelocityComponent::U, previousU, dt);
  const auto resultB = solver.solve(assemblyB.system);

  ASSERT_EQ(resultA.solution.size(), resultB.solution.size());
  for (Index i = 0; i < resultA.solution.size(); ++i) {
    EXPECT_EQ(resultA.solution[i], resultB.solution[i]);
  }
}
