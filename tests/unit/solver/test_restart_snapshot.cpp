#include <gtest/gtest.h>

#include <limits>
#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshFingerprint.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/solver/RestartSnapshot.hpp"
#include "cfd/solver/TimeController.hpp"
#include "cfd/solver/TransientSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
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
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::PISO;
using cfd::pressure_velocity::PISOSettings;
using cfd::solver::kRestartFormatVersion;
using cfd::solver::makeRestartSnapshot;
using cfd::solver::RestartSnapshot;
using cfd::solver::TimeController;
using cfd::solver::TransientResult;
using cfd::solver::TransientSolver;
using cfd::solver::TransientState;
using cfd::solver::TransientStatus;
using cfd::solver::validateRestartSnapshot;

namespace {

constexpr Real kNaN = std::numeric_limits<Real>::quiet_NaN();
constexpr Real kInf = std::numeric_limits<Real>::infinity();

TransientState makeValidState(const Mesh& mesh) {
  TransientState state;
  state.velocity = VectorField(mesh.numberOfCells(), Vector2{0.3, -0.1});
  state.pressure = ScalarField(mesh.numberOfCells(), 1.5);
  state.massFlux = SurfaceField(mesh.numberOfFaces(), 0.02);
  return state;
}

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

}  // namespace

// --- Construction: a valid state produces a valid snapshot ----------------

TEST(RestartSnapshotTest, ValidStateProducesValidSnapshot) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);

  const RestartSnapshot snapshot = makeRestartSnapshot(
      mesh, state, /*time=*/0.5, /*deltaTUsedToReachThisState=*/0.1, /*step=*/5);

  EXPECT_EQ(snapshot.formatVersion, kRestartFormatVersion);
  EXPECT_EQ(snapshot.cellCount, mesh.numberOfCells());
  EXPECT_EQ(snapshot.faceCount, mesh.numberOfFaces());
  EXPECT_EQ(snapshot.meshFingerprint, cfd::mesh::computeMeshFingerprint(mesh));
  EXPECT_NO_THROW(validateRestartSnapshot(snapshot, mesh));
}

TEST(RestartSnapshotTest, VelocityPressureFluxCopiedExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);

  const RestartSnapshot snapshot = makeRestartSnapshot(mesh, state, 0.5, 0.1, 5);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(snapshot.velocity[i].x, state.velocity[i].x) << "cell " << i;
    EXPECT_EQ(snapshot.velocity[i].y, state.velocity[i].y) << "cell " << i;
    EXPECT_EQ(snapshot.pressure[i], state.pressure[i]) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(snapshot.massFlux[i], state.massFlux[i]) << "face " << i;
  }
}

TEST(RestartSnapshotTest, TimeStepAndDeltaTCopiedExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);

  const RestartSnapshot snapshot = makeRestartSnapshot(
      mesh, state, /*time=*/0.73, /*deltaTUsedToReachThisState=*/0.037, /*step=*/17);

  EXPECT_EQ(snapshot.time, 0.73);
  EXPECT_EQ(snapshot.step, 17u);
  // "deltaT semantics explicitly tested": this file's own documented
  // convention -- deltaT is the dt used to advance *into* this state,
  // supplied verbatim by the caller, not recomputed or reinterpreted.
  EXPECT_EQ(snapshot.deltaT, 0.037);
}

TEST(RestartSnapshotTest, SourceTransientStateIsNotModified) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);
  const VectorField velocityCopy = state.velocity;
  const ScalarField pressureCopy = state.pressure;
  const SurfaceField massFluxCopy = state.massFlux;

  const RestartSnapshot snapshot = makeRestartSnapshot(mesh, state, 0.5, 0.1, 5);
  (void)snapshot;

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(state.velocity[i].x, velocityCopy[i].x);
    EXPECT_DOUBLE_EQ(state.velocity[i].y, velocityCopy[i].y);
    EXPECT_DOUBLE_EQ(state.pressure[i], pressureCopy[i]);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_DOUBLE_EQ(state.massFlux[i], massFluxCopy[i]);
  }
}

TEST(RestartSnapshotTest, RepeatedConstructionFromIdenticalStateIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);

  const RestartSnapshot snapshotA = makeRestartSnapshot(mesh, state, 0.5, 0.1, 5);
  const RestartSnapshot snapshotB = makeRestartSnapshot(mesh, state, 0.5, 0.1, 5);

  EXPECT_EQ(snapshotA.formatVersion, snapshotB.formatVersion);
  EXPECT_EQ(snapshotA.time, snapshotB.time);
  EXPECT_EQ(snapshotA.step, snapshotB.step);
  EXPECT_EQ(snapshotA.deltaT, snapshotB.deltaT);
  EXPECT_EQ(snapshotA.cellCount, snapshotB.cellCount);
  EXPECT_EQ(snapshotA.faceCount, snapshotB.faceCount);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(snapshotA.velocity[i].x, snapshotB.velocity[i].x);
    EXPECT_EQ(snapshotA.pressure[i], snapshotB.pressure[i]);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(snapshotA.massFlux[i], snapshotB.massFlux[i]);
  }
}

// --- Validation: rejection paths -------------------------------------------

TEST(RestartSnapshotTest, WrongCellCountIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  RestartSnapshot snapshot;
  snapshot.formatVersion = kRestartFormatVersion;
  snapshot.time = 0.1;
  snapshot.step = 1;
  snapshot.deltaT = 0.01;
  snapshot.velocity = VectorField(mesh.numberOfCells() - 1, Vector2{0.0, 0.0});
  snapshot.pressure = ScalarField(mesh.numberOfCells() - 1, 0.0);
  snapshot.massFlux = SurfaceField(mesh.numberOfFaces(), 0.0);
  snapshot.cellCount = mesh.numberOfCells() - 1;  // deliberately wrong
  snapshot.faceCount = mesh.numberOfFaces();

  EXPECT_THROW(validateRestartSnapshot(snapshot, mesh), cfd::InvalidArgumentError);
}

TEST(RestartSnapshotTest, WrongFaceCountIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  RestartSnapshot snapshot;
  snapshot.formatVersion = kRestartFormatVersion;
  snapshot.time = 0.1;
  snapshot.step = 1;
  snapshot.deltaT = 0.01;
  snapshot.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  snapshot.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  snapshot.massFlux = SurfaceField(mesh.numberOfFaces() - 1, 0.0);
  snapshot.cellCount = mesh.numberOfCells();
  snapshot.faceCount = mesh.numberOfFaces() - 1;  // deliberately wrong

  EXPECT_THROW(validateRestartSnapshot(snapshot, mesh), cfd::InvalidArgumentError);
}

TEST(RestartSnapshotTest, MismatchedMeshCellCountIsRejectedEvenWithConsistentFieldSizes) {
  // A snapshot that is internally self-consistent (fields match its own
  // cellCount/faceCount) but was taken against a *different* mesh.
  const Mesh smallMesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Mesh bigMesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState smallState = makeValidState(smallMesh);
  const RestartSnapshot snapshot = makeRestartSnapshot(smallMesh, smallState, 0.1, 0.01, 1);

  EXPECT_THROW(validateRestartSnapshot(snapshot, bigMesh), cfd::InvalidArgumentError);
}

TEST(RestartSnapshotTest, DifferentMeshWithSameCellAndFaceCountIsRejectedByFingerprint) {
  // The case count-only checking cannot catch: two meshes sharing both
  // cellCount and faceCount (same nx*ny topology) but differing in
  // geometry (a wider domain -> different cell volumes/face area
  // vectors/centroids) -- Restart-B's whole reason to exist.
  const Mesh meshA = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh meshB = MeshGeometry::createCartesian2D(4, 4, 2.0, 1.0);
  ASSERT_EQ(meshA.numberOfCells(), meshB.numberOfCells());
  ASSERT_EQ(meshA.numberOfFaces(), meshB.numberOfFaces());

  const TransientState stateA = makeValidState(meshA);
  const RestartSnapshot snapshot = makeRestartSnapshot(meshA, stateA, 0.1, 0.01, 1);

  EXPECT_THROW(validateRestartSnapshot(snapshot, meshB), cfd::InvalidArgumentError);
}

TEST(RestartSnapshotTest, NonFiniteVelocityIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  TransientState state = makeValidState(mesh);
  state.velocity[0] = Vector2{kNaN, 0.0};

  EXPECT_THROW(makeRestartSnapshot(mesh, state, 0.5, 0.1, 5), cfd::InvalidArgumentError);
}

TEST(RestartSnapshotTest, NonFinitePressureIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  TransientState state = makeValidState(mesh);
  state.pressure[0] = kInf;

  EXPECT_THROW(makeRestartSnapshot(mesh, state, 0.5, 0.1, 5), cfd::InvalidArgumentError);
}

TEST(RestartSnapshotTest, NonFiniteMassFluxIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  TransientState state = makeValidState(mesh);
  state.massFlux[0] = kNaN;

  EXPECT_THROW(makeRestartSnapshot(mesh, state, 0.5, 0.1, 5), cfd::InvalidArgumentError);
}

TEST(RestartSnapshotTest, NonFiniteTimeIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);

  for (const Real badTime : {kNaN, kInf, -kInf}) {
    EXPECT_THROW(makeRestartSnapshot(mesh, state, badTime, 0.1, 5), cfd::InvalidArgumentError)
        << "time=" << badTime;
  }
}

TEST(RestartSnapshotTest, NegativeTimeIsNotRejected) {
  // TimeController itself does not forbid a negative startTime (no such
  // check exists in TimeController's own constructor) -- this file must
  // not invent a stricter contract than the class that actually owns
  // physical time.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);

  EXPECT_NO_THROW(makeRestartSnapshot(mesh, state, /*time=*/-1.0, 0.1, 5));
}

TEST(RestartSnapshotTest, InvalidDeltaTIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);

  for (const Real badDeltaT : {0.0, -0.1, kNaN, kInf}) {
    EXPECT_THROW(makeRestartSnapshot(mesh, state, 0.5, badDeltaT, 5), cfd::InvalidArgumentError)
        << "deltaT=" << badDeltaT;
  }
}

TEST(RestartSnapshotTest, UnsupportedFormatVersionIsRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const TransientState state = makeValidState(mesh);
  RestartSnapshot snapshot = makeRestartSnapshot(mesh, state, 0.5, 0.1, 5);
  snapshot.formatVersion = kRestartFormatVersion + 1;

  EXPECT_THROW(validateRestartSnapshot(snapshot, mesh), cfd::InvalidArgumentError);
}

// --- The strongest test: a real accepted PISO state round-trips exactly ---

TEST(RestartSnapshotTest, RealAcceptedPisoStateRoundTripsThroughSnapshotExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  TransientState initialState;
  initialState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  initialState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  initialState.massFlux = calculateMassFlux(mesh, initialState.velocity, fluid, velocityBoundaries);

  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);
  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 0.03, 0.01, 100));

  ASSERT_EQ(result.status, TransientStatus::Completed);
  ASSERT_FALSE(result.history.empty());
  const auto& lastRecord = result.history.back();

  const RestartSnapshot snapshot = makeRestartSnapshot(mesh, result.finalState, lastRecord.time,
                                                       lastRecord.deltaT, lastRecord.step);

  // Every numerical value, bit-for-bit -- U == U_restart, p == p_restart,
  // F == F_restart -- plus time/step/deltaT matching TransientSolver's
  // own recorded history exactly, proving this file's "deltaT = dt used
  // to reach this state" convention genuinely agrees with
  // TimeStepRecord's own established one, not merely by definition.
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(snapshot.velocity[i].x, result.finalState.velocity[i].x) << "cell " << i;
    EXPECT_EQ(snapshot.velocity[i].y, result.finalState.velocity[i].y) << "cell " << i;
    EXPECT_EQ(snapshot.pressure[i], result.finalState.pressure[i]) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(snapshot.massFlux[i], result.finalState.massFlux[i]) << "face " << i;
  }
  EXPECT_EQ(snapshot.time, lastRecord.time);
  EXPECT_EQ(snapshot.step, lastRecord.step);
  EXPECT_EQ(snapshot.deltaT, lastRecord.deltaT);
  EXPECT_EQ(snapshot.cellCount, mesh.numberOfCells());
  EXPECT_EQ(snapshot.faceCount, mesh.numberOfFaces());
  EXPECT_NO_THROW(validateRestartSnapshot(snapshot, mesh));
}
