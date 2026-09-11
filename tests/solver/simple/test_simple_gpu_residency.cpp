// P6-GPU-001 -- Performance: proves SIMPLESettings::enableGpuResidency is
// wired into the *real* production solve loop (SIMPLE::solve()), not
// just exercised inside a standalone GPU benchmark/unit test --
// cfd::gpu::GpuResidencyManager has no CUDA dependency at the header
// level (see its own header comment), so this file is CUDA-independent
// and, like test_gpu_backend.cpp, always built and run in both build
// configurations. The real, device-observable reuse behavior is verified
// against genuine momentum/pressure-correction matrices assembled by a
// real SIMPLE outer loop -- not a synthetic grid matrix -- whenever a
// CUDA device is actually available at runtime.
#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;

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

// maxIterations, plus an outer tolerance so tight it can never be
// reached in a handful of iterations -- forces solve() to run *exactly*
// maxIterations outer iterations (status MaxIterations), which is what
// makes the reuse assertions below deterministic instead of depending on
// how quickly this particular cavity happens to converge.
SIMPLESettings makeFixedIterationCountSettings(Index maxIterations, bool enableGpuResidency) {
  SIMPLESettings settings;
  settings.maxIterations = maxIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-300;
  settings.pressureTolerance = 1e-300;
  settings.continuityTolerance = 1e-300;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  settings.enableGpuResidency = enableGpuResidency;
  return settings;
}

struct CavityCase {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  FluidProperties fluid;
};

CavityCase makeCavityCase() {
  Mesh mesh = MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  return CavityCase{std::move(mesh), std::move(velocityBoundaries), std::move(pressureBoundaries),
                    FluidProperties(1.0, 0.05)};
}

SIMPLEResult runCavity(const CavityCase& cavityCase, const SIMPLESettings& settings) {
  const SIMPLE simple(settings, /*referenceCell=*/0);
  const VectorField initialVelocity(cavityCase.mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(cavityCase.mesh.numberOfCells(), 0.0);
  return simple.solve(cavityCase.mesh, cavityCase.fluid, cavityCase.velocityBoundaries,
                      cavityCase.pressureBoundaries, initialVelocity, initialPressure);
}

}  // namespace

// Default settings (enableGpuResidency=false, matching every pre-P6-GPU-
// 001 caller) must issue zero GPU activity -- the residency path is
// strictly opt-in, so existing CPU execution is provably unaffected by
// this task, in both build configurations.
TEST(SIMPLEGpuResidencyTest, DisabledByDefaultIssuesNoGpuActivity) {
  const CavityCase cavityCase = makeCavityCase();
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLEResult result = runCavity(cavityCase, makeFixedIterationCountSettings(3, false));
  EXPECT_EQ(result.iterations, 3u);

  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.hostToDeviceCalls, 0u);
  EXPECT_EQ(stats.deviceToHostCalls, 0u);
  EXPECT_EQ(stats.allocations, 0u);
}

// Enabling GPU residency must never change SIMPLE's computed numerical
// result -- the mirrored device data is never read back into the solve
// (see SIMPLESettings::enableGpuResidency's own header comment); the CPU
// linear solvers remain the sole source of truth. Bit-for-bit equality
// is the correct bar here (not a tolerance-based comparison) because
// both runs execute the identical CPU code path with identical inputs.
TEST(SIMPLEGpuResidencyTest, EnablingResidencyDoesNotChangeTheComputedResult) {
  const CavityCase cavityCase = makeCavityCase();
  const SIMPLEResult withoutResidency =
      runCavity(cavityCase, makeFixedIterationCountSettings(4, false));
  const SIMPLEResult withResidency =
      runCavity(cavityCase, makeFixedIterationCountSettings(4, true));

  ASSERT_EQ(withoutResidency.iterations, withResidency.iterations);
  ASSERT_EQ(withoutResidency.velocity.size(), withResidency.velocity.size());
  for (Index i = 0; i < withoutResidency.velocity.size(); ++i) {
    EXPECT_EQ(withoutResidency.velocity[i].x, withResidency.velocity[i].x);
    EXPECT_EQ(withoutResidency.velocity[i].y, withResidency.velocity[i].y);
    EXPECT_EQ(withoutResidency.pressure[i], withResidency.pressure[i]);
  }
  EXPECT_EQ(withoutResidency.finalContinuityResidual, withResidency.finalContinuityResidual);
}

// The core P6-GPU-001 acceptance criterion, exercised against real
// production matrices/fields assembled by a real SIMPLE outer loop: more
// outer iterations must transfer more data (values keep changing every
// iteration) but must never allocate more device memory and must never
// reallocate -- allocation count is a function of how many distinct
// matrix/field keys SIMPLE mirrors (fixed at 6: momentum_u, momentum_v,
// pressure matrices + u, v, pressure fields), never of iteration count.
TEST(SIMPLEGpuResidencyTest, RepeatedOuterIterationsReuseGpuResidencyWithoutReallocating) {
  const CavityCase cavityCase = makeCavityCase();

  cfd::gpu::resetGpuExecutionStats();
  const SIMPLEResult shortRun = runCavity(cavityCase, makeFixedIterationCountSettings(3, true));
  ASSERT_EQ(shortRun.iterations, 3u);
  const cfd::gpu::GPUExecutionStats statsAfterShortRun = cfd::gpu::gpuExecutionStats();

  cfd::gpu::resetGpuExecutionStats();
  const SIMPLEResult longRun = runCavity(cavityCase, makeFixedIterationCountSettings(6, true));
  ASSERT_EQ(longRun.iterations, 6u);
  const cfd::gpu::GPUExecutionStats statsAfterLongRun = cfd::gpu::gpuExecutionStats();

  if (!cfd::gpu::cudaAvailable()) {
    // CPU-only build, or a CUDA build with no usable device: the
    // residency manager is inert regardless of the setting -- same
    // zero-activity guarantee as the disabled-by-default test above.
    EXPECT_EQ(statsAfterShortRun.hostToDeviceCalls, 0u);
    EXPECT_EQ(statsAfterLongRun.hostToDeviceCalls, 0u);
    return;
  }

  EXPECT_GT(statsAfterShortRun.hostToDeviceCalls, 0u);
  EXPECT_EQ(statsAfterShortRun.reallocations, 0u);
  EXPECT_EQ(statsAfterLongRun.reallocations, 0u);
  // Fixed set of keys -> fixed allocation count, independent of how many
  // outer iterations actually ran.
  EXPECT_EQ(statsAfterShortRun.allocations, statsAfterLongRun.allocations);
  // But the longer run really did do more (value-only) transfer work.
  EXPECT_LT(statsAfterShortRun.hostToDeviceCalls, statsAfterLongRun.hostToDeviceCalls);
}
