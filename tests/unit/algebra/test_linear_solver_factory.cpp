// P6-GPU-002 -- Performance: cfd::algebra::makeLinearSolver()'s backend-
// selection contract. Always built (CFDAlgebraTests has no CUDA
// dependency -- makeLinearSolver()/LinearSolverSettings/
// cfd::gpu::cudaAvailable() are all CUDA-independent headers), so this
// file exercises the *deterministic, logged, counted* CPU fallback in
// both build configurations: whatever cfd::gpu::cudaAvailable() answers
// on this machine/build, a GPU-backend request either genuinely runs on
// GPU (backendUsed == GPU, no fallback counted) or falls back to the
// CPU-equivalent solver (backendUsed == CPU, exactly one fallback
// counted) -- never anything in between, and never a thrown exception
// for "GPU unavailable" (that is the documented, non-error fallback
// path).
#include <gtest/gtest.h>

#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::CG;
using cfd::algebra::IdentityPreconditioner;
using cfd::algebra::JacobiPreconditioner;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::LinearSystem;
using cfd::algebra::makeLinearSolver;
using cfd::algebra::PreconditionerType;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

LinearSolverSettings strictSettings() {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-12;
  settings.relativeTolerance = 1e-12;
  settings.maxIterations = 1000;
  return settings;
}

// SPD tridiagonal-ish system, solvable by both CG and BiCGSTAB.
SparseMatrix makeSpdMatrix(Index n) {
  SparseMatrixBuilder builder(n, n);
  for (Index i = 0; i < n; ++i) {
    Real diagonal = 2.0;
    if (i > 0) {
      builder.add(i, i - 1, -1.0);
      diagonal += 1.0;
    }
    if (i + 1 < n) {
      builder.add(i, i + 1, -1.0);
      diagonal += 1.0;
    }
    builder.add(i, i, diagonal);
  }
  return builder.build();
}

}  // namespace

TEST(LinearSolverFactoryTest, DefaultSettingsSelectCpuBiCGSTAB) {
  const auto matrix = makeSpdMatrix(10);
  const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));

  // Default LinearSolverSettings (BiCGSTAB + CPU) must match a direct
  // BiCGSTAB construction bit-for-bit -- this is the exact behavior
  // every pre-P6-GPU-002 call site already got.
  const BiCGSTAB direct(strictSettings());
  const auto directResult = direct.solve(system);

  const auto factorySolver = makeLinearSolver(strictSettings());
  const auto factoryResult = factorySolver->solve(system);

  EXPECT_EQ(factoryResult.status, directResult.status);
  EXPECT_EQ(factoryResult.iterations, directResult.iterations);
  ASSERT_EQ(factoryResult.solution.size(), directResult.solution.size());
  for (Index i = 0; i < directResult.solution.size(); ++i) {
    EXPECT_EQ(factoryResult.solution[i], directResult.solution[i]);
  }
  EXPECT_EQ(factoryResult.backendUsed, LinearSolverBackend::CPU);
}

TEST(LinearSolverFactoryTest, CgTypeSelectsCpuCG) {
  const auto matrix = makeSpdMatrix(10);
  const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));

  LinearSolverSettings settings = strictSettings();
  settings.type = LinearSolverType::CG;

  const CG direct(strictSettings());
  const auto directResult = direct.solve(system);

  const auto factorySolver = makeLinearSolver(settings);
  const auto factoryResult = factorySolver->solve(system);

  EXPECT_EQ(factoryResult.status, directResult.status);
  ASSERT_EQ(factoryResult.solution.size(), directResult.solution.size());
  for (Index i = 0; i < directResult.solution.size(); ++i) {
    EXPECT_EQ(factoryResult.solution[i], directResult.solution[i]);
  }
  EXPECT_EQ(factoryResult.backendUsed, LinearSolverBackend::CPU);
}

TEST(LinearSolverFactoryTest, GpuBackendRequestNeverThrowsRegardlessOfDeviceAvailability) {
  LinearSolverSettings settings = strictSettings();
  settings.backend = LinearSolverBackend::GPU;

  EXPECT_NO_THROW({
    const auto solver = makeLinearSolver(settings);
    ASSERT_NE(solver, nullptr);
    const auto matrix = makeSpdMatrix(10);
    const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));
    const auto result = solver->solve(system);
    (void)result;
  });
}

TEST(LinearSolverFactoryTest,
     GpuBackendReportsAvailabilityConsistentlyAndFallsBackDeterministically) {
  const auto matrix = makeSpdMatrix(12);
  const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));

  LinearSolverSettings settings = strictSettings();
  settings.backend = LinearSolverBackend::GPU;

  cfd::gpu::resetGpuExecutionStats();
  const auto solver = makeLinearSolver(settings);
  const auto result = solver->solve(system);
  const auto& stats = cfd::gpu::gpuExecutionStats();

  if (cfd::gpu::cudaAvailable()) {
    // A usable device: GPU actually ran, no fallback counted.
    EXPECT_EQ(result.backendUsed, LinearSolverBackend::GPU);
    EXPECT_EQ(stats.gpuBackendFallbacks, 0u);
  } else {
    // No usable device (CPU-only build, or no device at runtime): the
    // deterministic, counted fallback to the CPU-equivalent solver --
    // never a silently-invalid or thrown result.
    EXPECT_EQ(result.backendUsed, LinearSolverBackend::CPU);
    EXPECT_EQ(stats.gpuBackendFallbacks, 1u);
    EXPECT_TRUE(result.converged());
  }
}

// ---------------------------------------------------------------------
// P6-GPU-003 -- Performance: PreconditionerType::Jacobi selection.
// ---------------------------------------------------------------------

TEST(LinearSolverFactoryTest, CpuJacobiPreconditionerTypeMatchesExplicitJacobiPreconditioner) {
  const auto matrix = makeSpdMatrix(10);
  const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));

  // Data-driven selection (settings.preconditioner, no explicit
  // shared_ptr) must produce the exact same solve as directly
  // constructing BiCGSTAB with an explicit JacobiPreconditioner --
  // makeLinearSolver() only decides *which* preconditioner object to
  // build, never a different algorithm.
  LinearSolverSettings settings = strictSettings();
  settings.preconditioner = PreconditionerType::Jacobi;
  const auto factorySolver = makeLinearSolver(settings);
  const auto factoryResult = factorySolver->solve(system);

  const BiCGSTAB direct(strictSettings(), std::make_shared<JacobiPreconditioner>());
  const auto directResult = direct.solve(system);

  EXPECT_EQ(factoryResult.status, directResult.status);
  EXPECT_TRUE(factoryResult.converged());
  ASSERT_EQ(factoryResult.solution.size(), directResult.solution.size());
  for (Index i = 0; i < directResult.solution.size(); ++i) {
    EXPECT_DOUBLE_EQ(factoryResult.solution[i], directResult.solution[i]);
  }
}

TEST(LinearSolverFactoryTest, ExplicitPreconditionerArgumentOverridesSettingsPreconditioner) {
  // An explicit caller-supplied preconditioner is documented to win over
  // settings.preconditioner (LinearSolverFactory.hpp's own header
  // comment) -- passing IdentityPreconditioner explicitly while
  // settings still says Jacobi must behave like plain identity
  // preconditioning (M = I), not Jacobi.
  const auto matrix = makeSpdMatrix(10);
  const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));

  LinearSolverSettings settings = strictSettings();
  settings.preconditioner = PreconditionerType::Jacobi;
  const auto factorySolver = makeLinearSolver(settings, std::make_shared<IdentityPreconditioner>());
  const auto factoryResult = factorySolver->solve(system);

  const BiCGSTAB unpreconditioned(strictSettings());
  const auto unpreconditionedResult = unpreconditioned.solve(system);

  EXPECT_EQ(factoryResult.iterations, unpreconditionedResult.iterations);
  ASSERT_EQ(factoryResult.solution.size(), unpreconditionedResult.solution.size());
  for (Index i = 0; i < unpreconditionedResult.solution.size(); ++i) {
    EXPECT_DOUBLE_EQ(factoryResult.solution[i], unpreconditionedResult.solution[i]);
  }
}

TEST(LinearSolverFactoryTest, GpuBackendJacobiRequestNeverThrowsAndConverges) {
  // Same "never throws regardless of device availability" contract as
  // GpuBackendRequestNeverThrowsRegardlessOfDeviceAvailability above, now
  // with Jacobi selected: on a CUDA build with a usable device this
  // exercises GpuCG/GpuBiCGSTAB's own GPU-resident Jacobi fast path
  // (cuda/kernels/GpuPreconditionerKernel.cu); otherwise it falls back to
  // CPU BiCGSTAB + JacobiPreconditioner via makeLinearSolver()'s own
  // documented fallback.
  const auto matrix = makeSpdMatrix(10);
  const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));

  LinearSolverSettings settings = strictSettings();
  settings.backend = LinearSolverBackend::GPU;
  settings.preconditioner = PreconditionerType::Jacobi;

  EXPECT_NO_THROW({
    const auto solver = makeLinearSolver(settings);
    ASSERT_NE(solver, nullptr);
    const auto result = solver->solve(system);
    EXPECT_TRUE(result.converged());
  });
}

TEST(LinearSolverFactoryTest, InvalidSettingsStillThrowRegardlessOfBackend) {
  LinearSolverSettings invalid = strictSettings();
  invalid.maxIterations = 0;

  EXPECT_THROW((void)makeLinearSolver(invalid), InvalidArgumentError);

  invalid = strictSettings();
  invalid.backend = LinearSolverBackend::GPU;
  invalid.maxIterations = 0;
  EXPECT_THROW((void)makeLinearSolver(invalid), InvalidArgumentError);
}
