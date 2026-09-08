// P2 -- Restart capability, Restart-E/F: resuming TransientSolver from a
// loaded RestartSnapshot, and the decisive regression -- a continuous run
// and a save/destroy/reload split run reach bit-identical final state.
#include <gtest/gtest.h>

#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/io/RestartReader.hpp"
#include "cfd/io/RestartWriter.hpp"
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
using cfd::io::RestartReader;
using cfd::io::RestartWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::PISO;
using cfd::pressure_velocity::PISOSettings;
using cfd::solver::makeRestartSnapshot;
using cfd::solver::RestartSnapshot;
using cfd::solver::TimeController;
using cfd::solver::TransientResult;
using cfd::solver::TransientSolver;
using cfd::solver::TransientState;
using cfd::solver::TransientStatus;

namespace {

std::filesystem::path tempPath(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "cfdapp_restart_integration";
  std::filesystem::create_directories(dir);
  return dir / name;
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

TransientState makeRestState(const Mesh& mesh, const FluidProperties& fluid,
                             const BoundaryConditionSet& velocityBoundaries) {
  TransientState state;
  state.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  state.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  state.massFlux = calculateMassFlux(mesh, state.velocity, fluid, velocityBoundaries);
  return state;
}

}  // namespace

TEST(RestartResumeTest, ResumedTransientSolverContinuesFromSnapshotStateAndTime) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const TransientSolver solver(piso, /*cflFailAbove=*/1000.0);

  // Run to a checkpoint at t=0.02 (2 steps of dt=0.01), save it.
  const TransientResult checkpoint = solver.solve(makeRestState(mesh, fluid, velocityBoundaries),
                                                  TimeController(0.0, 0.02, 0.01, 100));
  ASSERT_EQ(checkpoint.status, TransientStatus::Completed);
  ASSERT_EQ(checkpoint.history.back().step, 2u);
  const RestartSnapshot snapshot =
      makeRestartSnapshot(mesh, checkpoint.finalState, checkpoint.history.back().time,
                          checkpoint.history.back().deltaT, checkpoint.history.back().step);
  const auto path = tempPath("resume_basic.json");
  RestartWriter::write(path, snapshot);

  // Load it back and resume toward the *original* run's own endTime=0.03,
  // using the *original* startTime (0.0)/deltaT (0.01)/maxSteps (100) --
  // never a startTime shifted to the resume point (TimeController's own
  // doc comment on `startingStep` explains why).
  const RestartSnapshot loaded = RestartReader::read(path, mesh);
  TransientState resumedState;
  resumedState.velocity = loaded.velocity;
  resumedState.pressure = loaded.pressure;
  resumedState.massFlux = loaded.massFlux;
  const TimeController resumedController(0.0, 0.03, 0.01, 100, /*startingStep=*/loaded.step);

  const TransientResult resumed = solver.solve(resumedState, resumedController);

  EXPECT_EQ(resumed.status, TransientStatus::Completed);
  ASSERT_FALSE(resumed.history.empty());
  EXPECT_EQ(resumed.history.back().step, 3u);  // absolute step count, not reset to 1
  EXPECT_DOUBLE_EQ(resumed.history.back().time, 0.03);
}

// The decisive regression: a continuous run and a save/destroy/reload
// split run reach bit-identical final state, time, and step.
TEST(RestartResumeTest, ContinuousVsSplitRunIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);

  // --- Run A: continuous, t=0 -> t=0.05 in one call. ---
  TransientResult runA;
  {
    const TransientSolver solverA(piso, /*cflFailAbove=*/1000.0);
    runA = solverA.solve(makeRestState(mesh, fluid, velocityBoundaries),
                         TimeController(0.0, 0.05, 0.01, 100));
  }
  ASSERT_EQ(runA.status, TransientStatus::Completed);
  ASSERT_EQ(runA.history.size(), 5u);

  // --- Run B: split at t=0.02, genuinely through the restart file -- the
  // checkpoint's own runtime state is scoped out of existence before the
  // second half ever runs, so nothing but the file on disk carries state
  // across. ---
  const auto path = tempPath("continuous_vs_split.json");
  {
    const TransientSolver solverFirstHalf(piso, /*cflFailAbove=*/1000.0);
    const TransientResult checkpoint = solverFirstHalf.solve(
        makeRestState(mesh, fluid, velocityBoundaries), TimeController(0.0, 0.02, 0.01, 100));
    ASSERT_EQ(checkpoint.status, TransientStatus::Completed);
    const RestartSnapshot snapshot =
        makeRestartSnapshot(mesh, checkpoint.finalState, checkpoint.history.back().time,
                            checkpoint.history.back().deltaT, checkpoint.history.back().step);
    RestartWriter::write(path, snapshot);
    // checkpoint/snapshot/solverFirstHalf all go out of scope here --
    // the second half below reconstructs everything from `path` alone.
  }

  TransientResult runB;
  {
    const RestartSnapshot loaded = RestartReader::read(path, mesh);
    TransientState resumedState;
    resumedState.velocity = loaded.velocity;
    resumedState.pressure = loaded.pressure;
    resumedState.massFlux = loaded.massFlux;
    const TimeController resumedController(0.0, 0.05, 0.01, 100, loaded.step);

    const TransientSolver solverSecondHalf(piso, /*cflFailAbove=*/1000.0);
    runB = solverSecondHalf.solve(resumedState, resumedController);
  }
  ASSERT_EQ(runB.status, TransientStatus::Completed);
  ASSERT_EQ(runB.history.size(), 3u);  // steps 3, 4, 5 only -- resumed from step 2

  // Final velocity/pressure/authoritative mass flux, bit-for-bit.
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(runA.finalState.velocity[i].x, runB.finalState.velocity[i].x) << "cell " << i;
    EXPECT_EQ(runA.finalState.velocity[i].y, runB.finalState.velocity[i].y) << "cell " << i;
    EXPECT_EQ(runA.finalState.pressure[i], runB.finalState.pressure[i]) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(runA.finalState.massFlux[i], runB.finalState.massFlux[i]) << "face " << i;
  }

  // Final physical time and accepted step, bit-for-bit.
  EXPECT_EQ(runA.history.back().time, runB.history.back().time);
  EXPECT_EQ(runA.history.back().step, runB.history.back().step);

  // Every overlapping history entry (steps 3, 4, 5) matches exactly --
  // not just the final one.
  for (std::size_t i = 0; i < runB.history.size(); ++i) {
    const auto& a = runA.history[runA.history.size() - runB.history.size() + i];
    const auto& b = runB.history[i];
    EXPECT_EQ(a.step, b.step) << "history index " << i;
    EXPECT_EQ(a.time, b.time) << "history index " << i;
    EXPECT_EQ(a.deltaT, b.deltaT) << "history index " << i;
    EXPECT_EQ(a.maxCFL, b.maxCFL) << "history index " << i;
    EXPECT_EQ(a.continuityResidual, b.continuityResidual) << "history index " << i;
    EXPECT_EQ(a.massImbalance, b.massImbalance) << "history index " << i;
  }
}
