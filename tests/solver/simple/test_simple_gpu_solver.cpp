// P6-GPU-002 -- Performance: proves GPU CG/BiCGSTAB are selectable
// through the *real* production path -- SIMPLESettings::momentumSolver/
// pressureSolver's own backend field, translated by SIMPLE::solve()'s
// cfd::algebra::makeLinearSolver() call (see SIMPLE.cpp) -- not just a
// standalone GPU-solver unit test. cfd::algebra::LinearSolverFactory/
// cfd::gpu::cudaAvailable() have no CUDA dependency at the header level,
// so this file is CUDA-independent and, like test_simple_gpu_residency.
// cpp, always built and run in both configurations: on a machine with no
// usable CUDA device (or a CPU-only build), GPU backend requests
// deterministically fall back to CPU (see LinearSolverFactoryTest's own
// coverage of that contract) and this file's equivalence assertions
// still hold trivially (both runs take the identical CPU code path).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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
using cfd::algebra::LinearSolverBackend;
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
using cfd::pressure_velocity::SIMPLEStatus;

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

SIMPLESettings makeCavitySettings(LinearSolverBackend backend) {
  SIMPLESettings settings;
  settings.maxIterations = 3000;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.momentumSolver.backend = backend;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.backend = backend;
  return settings;
}

struct CavityCase {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  FluidProperties fluid;
};

CavityCase makeCavityCase(Index n) {
  Mesh mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  return CavityCase{std::move(mesh), std::move(velocityBoundaries), std::move(pressureBoundaries),
                    FluidProperties(1.0, 0.01)};
}

SIMPLEResult runCavity(const CavityCase& cavityCase, const SIMPLESettings& settings) {
  const SIMPLE simple(settings, /*referenceCell=*/0);
  const VectorField initialVelocity(cavityCase.mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(cavityCase.mesh.numberOfCells(), 0.0);
  return simple.solve(cavityCase.mesh, cavityCase.fluid, cavityCase.velocityBoundaries,
                      cavityCase.pressureBoundaries, initialVelocity, initialPressure);
}

}  // namespace

// The core P6-GPU-002 acceptance criterion: a production SIMPLE case can
// select the GPU backend and reproduce the CPU solution within a
// justified tolerance -- exercised through the real
// SIMPLESettings/SIMPLE::solve() path, on a canonical lid-driven cavity,
// not a synthetic linear system.
TEST(SIMPLEGpuSolverTest, GpuBackendReproducesCpuCavitySolutionWithinTolerance) {
  const CavityCase cavityCase = makeCavityCase(6);

  const SIMPLEResult cpuResult =
      runCavity(cavityCase, makeCavitySettings(LinearSolverBackend::CPU));
  const SIMPLEResult gpuResult =
      runCavity(cavityCase, makeCavitySettings(LinearSolverBackend::GPU));

  ASSERT_EQ(cpuResult.status, SIMPLEStatus::Converged);
  ASSERT_EQ(gpuResult.status, SIMPLEStatus::Converged);

  Real maxVelocityError = 0.0;
  Real maxPressureError = 0.0;
  ASSERT_EQ(cpuResult.velocity.size(), gpuResult.velocity.size());
  for (Index i = 0; i < cpuResult.velocity.size(); ++i) {
    maxVelocityError =
        std::max(maxVelocityError, std::abs(cpuResult.velocity[i].x - gpuResult.velocity[i].x));
    maxVelocityError =
        std::max(maxVelocityError, std::abs(cpuResult.velocity[i].y - gpuResult.velocity[i].y));
    maxPressureError =
        std::max(maxPressureError, std::abs(cpuResult.pressure[i] - gpuResult.pressure[i]));
  }

  // If GPU actually ran (a usable device was available), CPU and GPU
  // solve the same discretized system to the same linear-solver
  // tolerances (1e-10 abs / 1e-8 rel, see makeCavitySettings) but via a
  // different floating-point summation order (GPU block-reduction vs
  // CPU sequential dot product) -- some divergence is expected and
  // acceptable, not a defect; if GPU was unavailable and fell back to
  // CPU, both runs take the *identical* code path and must match
  // exactly. Either way, this tolerance is well within SIMPLE's own
  // production convergence tolerances (1e-6) used above.
  EXPECT_LT(maxVelocityError, 1e-6) << "max velocity component error";
  EXPECT_LT(maxPressureError, 1e-6) << "max pressure error";

  // TODO.md P6-GPU-001 wall-flux check, reused here as a sanity bound
  // that the GPU solution is itself physically valid, not just close to
  // the CPU one.
  for (const auto& patch : cavityCase.mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(gpuResult.massFlux[faceId], 0.0, 1e-8);
    }
  }
}

// A GPU-backend request must never change SIMPLE's convergence
// acceptance criteria -- same status enum, same residual/mass-imbalance
// fields, populated the same way regardless of which backend actually
// solved the linear systems.
TEST(SIMPLEGpuSolverTest, GpuBackendSatisfiesTheSameConvergenceCriteria) {
  const CavityCase cavityCase = makeCavityCase(6);
  const SIMPLEResult result = runCavity(cavityCase, makeCavitySettings(LinearSolverBackend::GPU));

  ASSERT_EQ(result.status, SIMPLEStatus::Converged);
  EXPECT_TRUE(result.converged());
  EXPECT_LE(result.finalUResidual, 1e-6);
  EXPECT_LE(result.finalVResidual, 1e-6);
  EXPECT_LE(result.finalPressureResidual, 1e-6);
  EXPECT_LE(result.finalContinuityResidual, 1e-6);
  EXPECT_LE(result.globalMassImbalance, 1e-6);
}

// P6-GPU-002's own reuse requirement, proven against the real production
// solve loop: repeated outer iterations of the *same* SIMPLE::solve()
// call reuse the GPU CG/BiCGSTAB solver instances' persistent device
// buffers -- allocation count is a function of how many distinct linear
// systems exist (2: momentum, pressure -- momentumSolver/pressureSolver
// are each one persistent solver instance for the whole solve() call),
// never of how many outer iterations ran.
TEST(SIMPLEGpuSolverTest, RepeatedOuterIterationsReuseGpuSolverBuffersWithoutReallocating) {
  const CavityCase cavityCase = makeCavityCase(10);
  SIMPLESettings settings = makeCavitySettings(LinearSolverBackend::GPU);
  // An unreachable outer tolerance forces every outer iteration allowed
  // by maxIterations to actually run, making the allocation-vs-iteration
  // -count comparison below deterministic (see
  // test_simple_gpu_residency.cpp's own identical technique).
  settings.velocityTolerance = 1e-300;
  settings.pressureTolerance = 1e-300;
  settings.continuityTolerance = 1e-300;

  settings.maxIterations = 3;
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLEResult shortRun = runCavity(cavityCase, settings);
  ASSERT_EQ(shortRun.iterations, 3u);
  const auto statsAfterShort = cfd::gpu::gpuExecutionStats();

  settings.maxIterations = 6;
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLEResult longRun = runCavity(cavityCase, settings);
  ASSERT_EQ(longRun.iterations, 6u);
  const auto statsAfterLong = cfd::gpu::gpuExecutionStats();

  if (!cfd::gpu::cudaAvailable()) {
    EXPECT_EQ(statsAfterShort.gpuLinearSolves, 0u);
    EXPECT_EQ(statsAfterLong.gpuLinearSolves, 0u);
    return;
  }

  // 3 solve() calls per outer iteration: momentumSolver->solve() once
  // for U and once for V (the same solver *instance* handles both
  // components -- see SIMPLE.cpp), plus pressureSolver->solve() once.
  EXPECT_EQ(statsAfterShort.gpuLinearSolves, 3u * 3u);
  EXPECT_EQ(statsAfterLong.gpuLinearSolves, 6u * 3u);
  EXPECT_EQ(statsAfterShort.reallocations, 0u);
  EXPECT_EQ(statsAfterLong.reallocations, 0u);
  // Two persistent solver instances (momentum, pressure) each allocate
  // their own device buffers once -- independent of iteration count, so
  // allocations must never grow with it. Not necessarily *equal* between
  // the short and long run: DeviceVectorOps.hpp's dot() reduction buffer
  // is a process-wide `static` cache (shared by every GpuCG/GpuBiCGSTAB
  // instance in this process, not just one solver's own persistent
  // buffers -- see its own header comment), so it is already
  // correctly-sized by the time the *second* of these two sequential
  // runCavity() calls executes, legitimately reporting one fewer
  // allocation than the first run that warmed it up.
  EXPECT_LE(statsAfterLong.allocations, statsAfterShort.allocations);
  EXPECT_GT(statsAfterShort.allocations, 0u);
}
