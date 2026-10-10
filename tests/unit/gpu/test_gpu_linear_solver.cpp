// P6-GPU-002 -- Performance: GpuCG/GpuBiCGSTAB correctness and CPU/GPU
// equivalence. Only compiled when CFDAPP_ENABLE_CUDA=ON (see this
// directory's own CMakeLists.txt); a CPU-only build never sees this
// file -- tests/unit/algebra/test_linear_solver_factory.cpp covers the
// always-built CPU-fallback contract instead. Same GTEST_SKIP()-if-
// no-device convention as test_device_buffer.cpp.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::CG;
using cfd::algebra::JacobiPreconditioner;
using cfd::algebra::LinearSolver;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::PreconditionerType;
using cfd::algebra::SolverResult;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) {
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
}

LinearSolverSettings strictSettings(Index maxIterations = 1000) {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-10;
  settings.relativeTolerance = 1e-10;
  settings.maxIterations = maxIterations;
  return settings;
}

// The same 5-point-stencil grid matrix used throughout P6-GPU-001's own
// tests -- symmetric positive-definite (diagonally dominant with a
// positive diagonal boost), representative of a real pressure-correction
// discretization. Suitable for CG.
SparseMatrix makeSpdGridMatrix(Index nx, Index ny, Real diagonalBoost = 0.1) {
  const Index n = nx * ny;
  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  const auto index = [&](Index i, Index j) { return j * nx + i; };
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Index p = index(i, j);
      Real diagonal = 0.0;
      if (i > 0) {
        builder.add(p, index(i - 1, j), -1.0);
        diagonal += 1.0;
      }
      if (i + 1 < nx) {
        builder.add(p, index(i + 1, j), -1.0);
        diagonal += 1.0;
      }
      if (j > 0) {
        builder.add(p, index(i, j - 1), -1.0);
        diagonal += 1.0;
      }
      if (j + 1 < ny) {
        builder.add(p, index(i, j + 1), -1.0);
        diagonal += 1.0;
      }
      builder.add(p, p, diagonal + diagonalBoost);
    }
  }
  return builder.build();
}

// A = [4 1 0; 2 3 1; 0 1 2] (not symmetric) -- test_bicgstab.cpp's own
// fixture, x_true = [1,2,3] -> b = [6,11,8].
SparseMatrix makeNonsymmetricMatrix() {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 2.0);
  builder.add(1, 1, 3.0);
  builder.add(1, 2, 1.0);
  builder.add(2, 1, 1.0);
  builder.add(2, 2, 2.0);
  return builder.build();
}

// All-zero coefficients (still a structurally valid CSR pattern) -- A*x
// == 0 for every x, so the very first Ap/v the algorithm computes is the
// zero vector: pAp (CG) / rHatDotV (BiCGSTAB) are exactly 0, a
// deterministic, reliable breakdown trigger for both methods without
// depending on floating-point happenstance.
SparseMatrix makeZeroMatrix(Index n) {
  SparseMatrixBuilder builder(n, n);
  for (Index i = 0; i < n; ++i) builder.add(i, i, 0.0);
  return builder.build();
}

Vector makeVector(Index n, Real seed) {
  Vector v(n);
  for (Index i = 0; i < n; ++i) v[i] = std::sin(seed * static_cast<Real>(i + 1));
  return v;
}

struct Equivalence {
  Real maxAbsError{};
  Real maxRelError{};
};

Equivalence compareSolutions(const Vector& cpu, const Vector& gpu) {
  Equivalence eq;
  for (Index i = 0; i < cpu.size(); ++i) {
    const Real absError = std::abs(cpu[i] - gpu[i]);
    eq.maxAbsError = std::max(eq.maxAbsError, absError);
    if (std::abs(cpu[i]) > 1e-8) {
      eq.maxRelError = std::max(eq.maxRelError, absError / std::abs(cpu[i]));
    }
  }
  return eq;
}

}  // namespace

// ---------------------------------------------------------------------
// GpuCG
// ---------------------------------------------------------------------

TEST(GpuCGTest, MakeGpuCGReturnsNonNullWithAUsableDevice) {
  skipIfNoDevice();
  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);
}

TEST(GpuCGTest, SolvesSpdSystemAndMatchesBackendUsed) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(20, 20);
  const Vector x = makeVector(matrix.columns(), 0.37);
  const Vector b = matrix.multiply(x);
  const LinearSystem system(matrix, b);

  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system);

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.backendUsed, LinearSolverBackend::GPU);
  const Equivalence eq = compareSolutions(x, result.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6);
}

TEST(GpuCGTest, NonzeroInitialGuessConverges) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(15, 15);
  const Vector xTrue = makeVector(matrix.columns(), 0.71);
  const Vector b = matrix.multiply(xTrue);
  const LinearSystem system(matrix, b);

  Vector initialGuess(matrix.columns());
  for (Index i = 0; i < initialGuess.size(); ++i) initialGuess[i] = xTrue[i] * 0.9;

  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system, initialGuess);

  EXPECT_TRUE(result.converged());
  const Equivalence eq = compareSolutions(xTrue, result.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6);
}

TEST(GpuCGTest, ZeroRhsConvergesImmediately) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(10, 10);
  const LinearSystem system(matrix, Vector(matrix.rows(), 0.0));

  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system, Vector(matrix.columns(), 0.0));

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.iterations, 0u);
  EXPECT_EQ(result.finalResidual, 0.0);
}

TEST(GpuCGTest, MaxIterationsIsReportedWhenTooFewIterationsAllowed) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(30, 30);
  const Vector x = makeVector(matrix.columns(), 1.3);
  const LinearSystem system(matrix, matrix.multiply(x));

  const auto solver = cfd::gpu::makeGpuCG(strictSettings(/*maxIterations=*/1));
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::MaxIterations);
  EXPECT_EQ(result.iterations, 1u);
}

TEST(GpuCGTest, ZeroMatrixTriggersBreakdown) {
  skipIfNoDevice();
  const auto matrix = makeZeroMatrix(5);
  const LinearSystem system(matrix, Vector(5, 1.0));

  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::Breakdown);
}

TEST(GpuCGTest, NonFiniteInputRejectedBeforeAnyGpuWork) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(5, 5);
  const LinearSystem system(matrix, Vector(matrix.rows(), 1.0));

  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system, Vector(2, 0.0));  // wrong size

  EXPECT_EQ(result.status, SolverStatus::NonFiniteInput);
}

TEST(GpuCGTest, MatchesCpuCgWithinTolerance) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(40, 40);
  const Vector x = makeVector(matrix.columns(), 0.53);
  const LinearSystem system(matrix, matrix.multiply(x));

  const CG cpuSolver(strictSettings());
  const auto cpuResult = cpuSolver.solve(system);

  const auto gpuSolver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(gpuSolver, nullptr);
  const auto gpuResult = gpuSolver->solve(system);

  ASSERT_TRUE(cpuResult.converged());
  ASSERT_TRUE(gpuResult.converged());
  EXPECT_EQ(cpuResult.backendUsed, LinearSolverBackend::CPU);
  EXPECT_EQ(gpuResult.backendUsed, LinearSolverBackend::GPU);

  const Equivalence eq = compareSolutions(cpuResult.solution, gpuResult.solution);
  // GPU/CPU iteration counts need not match bit-for-bit (this task's own
  // "do not require bit-identical iteration counts" allowance) -- both
  // must satisfy the same convergence tolerance and agree on the
  // solution itself.
  EXPECT_LT(eq.maxAbsError, 1e-6) << "max abs error";
  EXPECT_LT(eq.maxRelError, 1e-6) << "max rel error";
}

TEST(GpuCGTest, RepeatedSolveReusesPersistentBuffersWithoutReallocating) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(25, 25);
  const Vector x = makeVector(matrix.columns(), 0.19);
  const LinearSystem system(matrix, matrix.multiply(x));

  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);

  // First call: genuine first-time allocation of every persistent
  // buffer (matrix structure+values, x/b/r/p/ap/z).
  const auto first = solver->solve(system);
  ASSERT_TRUE(first.converged());

  cfd::gpu::resetGpuExecutionStats();
  // Repeated calls on the SAME solver instance, same system shape --
  // must reuse every device buffer (P6-GPU-001's pipeline), not
  // reallocate.
  constexpr int kRepeats = 4;
  for (int i = 0; i < kRepeats; ++i) {
    const auto result = solver->solve(system);
    EXPECT_TRUE(result.converged());
  }

  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
  EXPECT_EQ(stats.gpuLinearSolves, static_cast<std::uint64_t>(kRepeats));
}

TEST(GpuCGTest, WorksWithAnExistingCpuSidePreconditioner) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(15, 15);
  const Vector x = makeVector(matrix.columns(), 0.83);
  const LinearSystem system(matrix, matrix.multiply(x));

  auto preconditioner = std::make_shared<JacobiPreconditioner>();
  const auto solver = cfd::gpu::makeGpuCG(strictSettings(), preconditioner);
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system);

  EXPECT_TRUE(result.converged());
  const Equivalence eq = compareSolutions(x, result.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6);
}

// ---------------------------------------------------------------------
// GpuBiCGSTAB
// ---------------------------------------------------------------------

TEST(GpuBiCGSTABTest, MakeGpuBiCGSTABReturnsNonNullWithAUsableDevice) {
  skipIfNoDevice();
  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(solver, nullptr);
}

TEST(GpuBiCGSTABTest, SolvesNonsymmetricSystem) {
  skipIfNoDevice();
  const LinearSystem system(makeNonsymmetricMatrix(), Vector{6.0, 11.0, 8.0});

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system);

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.backendUsed, LinearSolverBackend::GPU);
  EXPECT_NEAR(result.solution[0], 1.0, 1e-6);
  EXPECT_NEAR(result.solution[1], 2.0, 1e-6);
  EXPECT_NEAR(result.solution[2], 3.0, 1e-6);
}

TEST(GpuBiCGSTABTest, NonzeroInitialGuessConverges) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(15, 15);  // SPD is also a valid (if easy) BiCGSTAB case
  const Vector xTrue = makeVector(matrix.columns(), 0.61);
  const LinearSystem system(matrix, matrix.multiply(xTrue));

  Vector initialGuess(matrix.columns());
  for (Index i = 0; i < initialGuess.size(); ++i) initialGuess[i] = xTrue[i] * 1.1;

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system, initialGuess);

  EXPECT_TRUE(result.converged());
  const Equivalence eq = compareSolutions(xTrue, result.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6);
}

TEST(GpuBiCGSTABTest, ZeroRhsConvergesImmediately) {
  skipIfNoDevice();
  const LinearSystem system(makeNonsymmetricMatrix(), Vector(3, 0.0));

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system, Vector(3, 0.0));

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.iterations, 0u);
}

TEST(GpuBiCGSTABTest, MaxIterationsIsReportedWhenTooFewIterationsAllowed) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(30, 30);
  const Vector x = makeVector(matrix.columns(), 1.7);
  const LinearSystem system(matrix, matrix.multiply(x));

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings(/*maxIterations=*/1));
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::MaxIterations);
  EXPECT_EQ(result.iterations, 1u);
}

TEST(GpuBiCGSTABTest, ZeroMatrixTriggersBreakdown) {
  skipIfNoDevice();
  const auto matrix = makeZeroMatrix(5);
  const LinearSystem system(matrix, Vector(5, 1.0));

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::Breakdown);
}

TEST(GpuBiCGSTABTest, NonFiniteInputRejectedBeforeAnyGpuWork) {
  skipIfNoDevice();
  const LinearSystem system(makeNonsymmetricMatrix(), Vector(3, 1.0));

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(solver, nullptr);
  const SolverResult result = solver->solve(system, Vector(2, 0.0));  // wrong size

  EXPECT_EQ(result.status, SolverStatus::NonFiniteInput);
}

TEST(GpuBiCGSTABTest, MatchesCpuBiCGSTABWithinTolerance) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(35, 35);
  const Vector x = makeVector(matrix.columns(), 0.29);
  const LinearSystem system(matrix, matrix.multiply(x));

  const BiCGSTAB cpuSolver(strictSettings());
  const auto cpuResult = cpuSolver.solve(system);

  const auto gpuSolver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(gpuSolver, nullptr);
  const auto gpuResult = gpuSolver->solve(system);

  ASSERT_TRUE(cpuResult.converged());
  ASSERT_TRUE(gpuResult.converged());
  EXPECT_EQ(cpuResult.backendUsed, LinearSolverBackend::CPU);
  EXPECT_EQ(gpuResult.backendUsed, LinearSolverBackend::GPU);

  const Equivalence eq = compareSolutions(cpuResult.solution, gpuResult.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6) << "max abs error";
  EXPECT_LT(eq.maxRelError, 1e-6) << "max rel error";
}

TEST(GpuBiCGSTABTest, MatchesCpuOnANonsymmetricSystem) {
  skipIfNoDevice();
  const LinearSystem system(makeNonsymmetricMatrix(), Vector{6.0, 11.0, 8.0});

  const BiCGSTAB cpuSolver(strictSettings());
  const auto cpuResult = cpuSolver.solve(system);

  const auto gpuSolver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(gpuSolver, nullptr);
  const auto gpuResult = gpuSolver->solve(system);

  ASSERT_TRUE(cpuResult.converged());
  ASSERT_TRUE(gpuResult.converged());
  const Equivalence eq = compareSolutions(cpuResult.solution, gpuResult.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6);
}

TEST(GpuBiCGSTABTest, RepeatedSolveReusesPersistentBuffersWithoutReallocating) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(25, 25);
  const Vector x = makeVector(matrix.columns(), 0.47);
  const LinearSystem system(matrix, matrix.multiply(x));

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(solver, nullptr);

  const auto first = solver->solve(system);
  ASSERT_TRUE(first.converged());

  cfd::gpu::resetGpuExecutionStats();
  constexpr int kRepeats = 4;
  for (int i = 0; i < kRepeats; ++i) {
    const auto result = solver->solve(system);
    EXPECT_TRUE(result.converged());
  }

  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
  EXPECT_EQ(stats.gpuLinearSolves, static_cast<std::uint64_t>(kRepeats));
}

// ---------------------------------------------------------------------
// GPU-resident Jacobi preconditioning (P6-GPU-003)
// ---------------------------------------------------------------------

namespace {

LinearSolverSettings gpuJacobiSettings(Index maxIterations = 1000) {
  LinearSolverSettings settings = strictSettings(maxIterations);
  settings.backend = LinearSolverBackend::GPU;
  settings.preconditioner = PreconditionerType::Jacobi;
  return settings;
}

}  // namespace

TEST(GpuJacobiTest, CgMatchesCpuJacobiWithinTolerance) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(40, 40);
  const Vector x = makeVector(matrix.columns(), 0.53);
  const LinearSystem system(matrix, matrix.multiply(x));

  // CPU reference: BiCGSTAB.cpp/CG.cpp's own existing (host-Vector-based)
  // JacobiPreconditioner, unaffected by this task.
  const CG cpuSolver(strictSettings(), std::make_shared<JacobiPreconditioner>());
  const auto cpuResult = cpuSolver.solve(system);

  // GPU: settings.preconditioner == Jacobi with no explicit
  // preconditioner argument -- exercises GpuCG's own GPU-resident
  // diag(A)^-1 fast path (cuda/kernels/GpuPreconditionerKernel.cu), not
  // the generic CPU-round-trip escape hatch.
  const auto gpuSolver = cfd::gpu::makeGpuCG(gpuJacobiSettings());
  ASSERT_NE(gpuSolver, nullptr);
  const auto gpuResult = gpuSolver->solve(system);

  ASSERT_TRUE(cpuResult.converged());
  ASSERT_TRUE(gpuResult.converged());
  EXPECT_EQ(gpuResult.backendUsed, LinearSolverBackend::GPU);

  const Equivalence eq = compareSolutions(cpuResult.solution, gpuResult.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6) << "max abs error";
  EXPECT_LT(eq.maxRelError, 1e-6) << "max rel error";
}

TEST(GpuJacobiTest, BiCGSTABMatchesCpuJacobiWithinTolerance) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(35, 35);
  const Vector x = makeVector(matrix.columns(), 0.29);
  const LinearSystem system(matrix, matrix.multiply(x));

  const BiCGSTAB cpuSolver(strictSettings(), std::make_shared<JacobiPreconditioner>());
  const auto cpuResult = cpuSolver.solve(system);

  const auto gpuSolver = cfd::gpu::makeGpuBiCGSTAB(gpuJacobiSettings());
  ASSERT_NE(gpuSolver, nullptr);
  const auto gpuResult = gpuSolver->solve(system);

  ASSERT_TRUE(cpuResult.converged());
  ASSERT_TRUE(gpuResult.converged());
  EXPECT_EQ(gpuResult.backendUsed, LinearSolverBackend::GPU);

  const Equivalence eq = compareSolutions(cpuResult.solution, gpuResult.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6) << "max abs error";
  EXPECT_LT(eq.maxRelError, 1e-6) << "max rel error";
}

TEST(GpuJacobiTest, NonsymmetricSystemMatchesCpuJacobi) {
  skipIfNoDevice();
  const LinearSystem system(makeNonsymmetricMatrix(), Vector{6.0, 11.0, 8.0});

  const BiCGSTAB cpuSolver(strictSettings(), std::make_shared<JacobiPreconditioner>());
  const auto cpuResult = cpuSolver.solve(system);

  const auto gpuSolver = cfd::gpu::makeGpuBiCGSTAB(gpuJacobiSettings());
  ASSERT_NE(gpuSolver, nullptr);
  const auto gpuResult = gpuSolver->solve(system);

  ASSERT_TRUE(cpuResult.converged());
  ASSERT_TRUE(gpuResult.converged());
  const Equivalence eq = compareSolutions(cpuResult.solution, gpuResult.solution);
  EXPECT_LT(eq.maxAbsError, 1e-6);
}

TEST(GpuJacobiTest, ZeroDiagonalMatrixReportsInvalidSystemNotAnException) {
  skipIfNoDevice();
  // All-zero CSR -- zero diagonal at every row. Jacobi's build step must
  // reject this (computeInverseDiagonal throws InvalidArgumentError
  // internally) and GpuCG must translate that into
  // SolverStatus::InvalidSystem, not let the exception escape solve().
  const auto matrix = makeZeroMatrix(5);
  const LinearSystem system(matrix, Vector(5, 1.0));

  const auto solver = cfd::gpu::makeGpuCG(gpuJacobiSettings());
  ASSERT_NE(solver, nullptr);

  SolverResult result;
  EXPECT_NO_THROW(result = solver->solve(system));
  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::InvalidSystem);
}

TEST(GpuJacobiTest, BiCGSTABZeroDiagonalMatrixReportsInvalidSystem) {
  skipIfNoDevice();
  const auto matrix = makeZeroMatrix(5);
  const LinearSystem system(matrix, Vector(5, 1.0));

  const auto solver = cfd::gpu::makeGpuBiCGSTAB(gpuJacobiSettings());
  ASSERT_NE(solver, nullptr);

  SolverResult result;
  EXPECT_NO_THROW(result = solver->solve(system));
  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::InvalidSystem);
}

TEST(GpuJacobiTest, RepeatedSolveReusesPersistentDiagonalBufferWithoutReallocating) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(25, 25);
  const Vector x = makeVector(matrix.columns(), 0.19);
  const LinearSystem system(matrix, matrix.multiply(x));

  const auto solver = cfd::gpu::makeGpuCG(gpuJacobiSettings());
  ASSERT_NE(solver, nullptr);

  // First call: genuine first-time allocation of every persistent
  // buffer, including the Jacobi inverse-diagonal buffer.
  const auto first = solver->solve(system);
  ASSERT_TRUE(first.converged());

  cfd::gpu::resetGpuExecutionStats();
  // Repeated solves against the same matrix shape must rebuild the
  // diagonal's *values* (matrix coefficients can change every outer
  // SIMPLE iteration in production -- buildJacobiDiagonal() re-extracts
  // and re-uploads every call, matching Preconditioner::build()'s own
  // per-solve() contract) but never grow/reallocate the device buffer
  // itself, and never allocate anything beyond it.
  constexpr int kRepeats = 4;
  for (int i = 0; i < kRepeats; ++i) {
    const auto result = solver->solve(system);
    EXPECT_TRUE(result.converged());
  }

  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
  EXPECT_EQ(stats.gpuLinearSolves, static_cast<std::uint64_t>(kRepeats));
  // Setup (host extract + one upload) and apply (device kernel) both ran
  // -- proves the fast path actually executed, not silently skipped.
  EXPECT_GT(stats.preconditionerSetupSeconds, 0.0);
  EXPECT_GT(stats.preconditionerApplySeconds, 0.0);
}

TEST(GpuJacobiTest, RebuildsDiagonalWhenMatrixValuesChange) {
  skipIfNoDevice();
  // Same sparsity structure, different coefficients -- proves the
  // solver's own buildPreconditionerFailed()/buildJacobiDiagonal() call
  // re-extracts the diagonal from the *current* matrix passed to solve(),
  // not a value captured on a prior call to the same solver instance.
  SparseMatrixBuilder builderA(2, 2);
  builderA.add(0, 0, 2.0);
  builderA.add(1, 1, 4.0);
  const LinearSystem systemA(builderA.build(), Vector{2.0, 8.0});

  const auto solver = cfd::gpu::makeGpuCG(gpuJacobiSettings());
  ASSERT_NE(solver, nullptr);
  const auto resultA = solver->solve(systemA);
  ASSERT_TRUE(resultA.converged());
  EXPECT_NEAR(resultA.solution[0], 1.0, 1e-8);
  EXPECT_NEAR(resultA.solution[1], 2.0, 1e-8);

  SparseMatrixBuilder builderB(2, 2);
  builderB.add(0, 0, 5.0);
  builderB.add(1, 1, 10.0);
  const LinearSystem systemB(builderB.build(), Vector{10.0, 30.0});
  const auto resultB = solver->solve(systemB);
  ASSERT_TRUE(resultB.converged());
  EXPECT_NEAR(resultB.solution[0], 2.0, 1e-8);
  EXPECT_NEAR(resultB.solution[1], 3.0, 1e-8);
}

TEST(GpuJacobiTest, ReducesIterationCountOnAPoorlyConditionedSystem) {
  skipIfNoDevice();
  // A near-singular diagonal boost (0.001, versus the 0.1 every other
  // test in this file uses) makes this grid matrix markedly less
  // well-conditioned -- exactly the case Jacobi preconditioning is meant
  // to help: it should never need *more* iterations than unpreconditioned
  // CG to reach the same tolerance. (See this task's own "do not assume
  // fewer iterations always means faster" caveat -- iteration count
  // alone is not the runtime metric; the benchmark tool under
  // benchmarks/gpu covers wall-clock time separately.)
  const auto matrix = makeSpdGridMatrix(30, 30, /*diagonalBoost=*/0.001);
  const Vector x = makeVector(matrix.columns(), 0.61);
  const LinearSystem system(matrix, matrix.multiply(x));

  LinearSolverSettings plainSettings = gpuJacobiSettings();
  plainSettings.preconditioner = PreconditionerType::None;
  const auto plainSolver = cfd::gpu::makeGpuCG(plainSettings);
  ASSERT_NE(plainSolver, nullptr);
  const auto plainResult = plainSolver->solve(system);

  const auto jacobiSolver = cfd::gpu::makeGpuCG(gpuJacobiSettings());
  ASSERT_NE(jacobiSolver, nullptr);
  const auto jacobiResult = jacobiSolver->solve(system);

  ASSERT_TRUE(plainResult.converged());
  ASSERT_TRUE(jacobiResult.converged());
  EXPECT_LE(jacobiResult.iterations, plainResult.iterations)
      << "Jacobi iterations=" << jacobiResult.iterations
      << " plain iterations=" << plainResult.iterations;
}

// ---------------------------------------------------------------------
// Instrumentation
// ---------------------------------------------------------------------

TEST(GpuLinearSolverInstrumentationTest, RecordsSolveTimingAndIterationCounts) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(30, 30);
  const Vector x = makeVector(matrix.columns(), 0.13);
  const LinearSystem system(matrix, matrix.multiply(x));

  const auto solver = cfd::gpu::makeGpuCG(strictSettings());
  ASSERT_NE(solver, nullptr);

  cfd::gpu::resetGpuExecutionStats();
  const auto result = solver->solve(system);
  ASSERT_TRUE(result.converged());
  ASSERT_GT(result.iterations, 0u);

  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_GT(stats.gpuSolveSeconds, 0.0);
  EXPECT_GT(stats.dotSeconds, 0.0);
  EXPECT_GT(stats.vectorOpSeconds, 0.0);
  EXPECT_EQ(stats.gpuLinearSolves, 1u);
  EXPECT_EQ(stats.gpuLinearSolverIterations, static_cast<std::uint64_t>(result.iterations));
}

// ---------------------------------------------------------------------
// DEBT-GPU-001 -- GPU BiCGSTAB Krylov restart parity with the CPU reference
// ---------------------------------------------------------------------
//
// cfd::algebra::BiCGSTAB restarts the Krylov sequence when an inner product of
// the recurrence has cancelled to the rounding level of its own terms. The GPU
// solver returned Breakdown at that point instead, which is what blocked P16:
// on bit-identical turbulence transport systems the CPU converged and the GPU
// stopped mid-descent
// (results/p16/turbulence/gpu_equivalence/krylov_blocker/findings.md).
//
// These tests pin the four properties that distinguish a faithful restart from
// a retry loop: it is taken only when it can help, it costs no extra iteration
// budget, it is inert on healthy systems, and it is deterministic. The
// ladder-scale evidence that restarting actually rescues real systems lives in
// test_krylov_blocker_reproducer.cpp.

namespace {

// A convection-dominated 2D grid operator: the symmetric 5-point diffusion
// stencil plus an ANTISYMMETRIC east/west convection coupling of strength
// kConvection. That antisymmetric part is what drives the recurrence into a
// qualifying cancellation: s^T A s sees only the symmetric part, while
// sum_i |(As)_i s_i| grows with the convection, so (t, s) eventually cancels to
// the rounding level of its own terms while the iterate is still healthy --
// exactly the situation a restart exists for. It is the same character of
// system as the turbulence transport equations that exposed the defect
// (strongly nonsymmetric, convection dominated), but built from nothing but a
// grid, so this test does not depend on the turbulence code at all.
//
// The parameters below are not arbitrary: they were chosen so that BOTH
// solvers converge AND both restart on the way. Weaker convection converges
// without ever restarting (the test would be vacuous, which the assertions
// below catch); much stronger convection breaks down on both.
constexpr Index kRestartNx = 20;
constexpr Real kRestartConvection = 8.0;
constexpr Real kRestartDiagonalBoost = 0.1;

SparseMatrix makeRestartingMatrix() {
  const Index nx = kRestartNx;
  const Index n = nx * nx;
  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  const auto id = [&](Index i, Index j) { return j * nx + i; };
  for (Index j = 0; j < nx; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Index p = id(i, j);
      Real diagonal = kRestartDiagonalBoost;
      if (i > 0) {
        builder.add(p, id(i - 1, j), -1.0 - kRestartConvection);
        diagonal += 1.0;
      }
      if (i + 1 < nx) {
        builder.add(p, id(i + 1, j), -1.0 + kRestartConvection);
        diagonal += 1.0;
      }
      if (j > 0) {
        builder.add(p, id(i, j - 1), -1.0);
        diagonal += 1.0;
      }
      if (j + 1 < nx) {
        builder.add(p, id(i, j + 1), -1.0);
        diagonal += 1.0;
      }
      builder.add(p, p, diagonal);
    }
  }
  return builder.build();
}

LinearSystem makeRestartingSystem() {
  const SparseMatrix matrix = makeRestartingMatrix();
  return LinearSystem(matrix, makeVector(matrix.columns(), 0.37));
}

}  // namespace

TEST(GpuBiCGSTABRestartTest, RestartsWhereTheCpuReferenceRestarts) {
  skipIfNoDevice();
  const LinearSystem system = makeRestartingSystem();

  const auto gpu = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(gpu, nullptr);
  const SolverResult gpuResult = gpu->solve(system);

  const BiCGSTAB cpu(strictSettings());
  const SolverResult cpuResult = cpu.solve(system);

  // Non-vacuity: if this system stopped reaching the restart branch the test
  // would pass while checking nothing, so the trigger is asserted first.
  ASSERT_GE(cpuResult.restarts, Index{1})
      << "the CPU reference no longer restarts on this system -- the trigger is gone and this "
         "test proves nothing about the GPU";

  EXPECT_GE(gpuResult.restarts, Index{1})
      << "the CPU restarted " << cpuResult.restarts
      << " time(s) on this system and the GPU restarted none -- status "
      << static_cast<int>(gpuResult.status);
  // The two solvers reduce in different orders, so they need not restart the
  // same number of times or land on the same iteration. They must agree on the
  // OUTCOME, which is the property the blocker violated.
  // Both solvers CONVERGE on this system, and both need a restart to do it.
  // That is the property the blocker violated: the GPU stopped where the CPU
  // restarted and went on to converge.
  EXPECT_EQ(cpuResult.status, SolverStatus::Converged);
  EXPECT_EQ(gpuResult.status, SolverStatus::Converged);
  EXPECT_LE(gpuResult.iterations, strictSettings().maxIterations);
}

TEST(GpuBiCGSTABRestartTest, RestartsDoNotBuyExtraIterationBudget) {
  skipIfNoDevice();
  const LinearSystem system = makeRestartingSystem();

  // A budget below what this system needs, but large enough that restarts do
  // happen inside it. A restart completes no iteration, so it must not become
  // a way to keep working past maxIterations: the solver reports MaxIterations
  // and stops, rather than restarting indefinitely at no cost to the budget.
  constexpr Index kBudget = 250;
  const auto gpu = cfd::gpu::makeGpuBiCGSTAB(strictSettings(kBudget));
  ASSERT_NE(gpu, nullptr);
  const SolverResult result = gpu->solve(system);

  ASSERT_GE(result.restarts, Index{1})
      << "no restart happened within the budget, so this does not test what a restart costs";
  EXPECT_EQ(result.status, SolverStatus::MaxIterations);
  EXPECT_LE(result.iterations, kBudget);
  for (const Real residual : result.residualHistory) EXPECT_TRUE(std::isfinite(residual));
}

TEST(GpuBiCGSTABRestartTest, HealthySystemNeverRestarts) {
  skipIfNoDevice();
  const auto matrix = makeSpdGridMatrix(20, 20);
  const Vector x = makeVector(matrix.columns(), 0.31);
  const LinearSystem system(matrix, matrix.multiply(x));

  const auto gpu = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(gpu, nullptr);
  const SolverResult result = gpu->solve(system);

  ASSERT_TRUE(result.converged());
  // The restart must be inert where nothing breaks down: a system that never
  // reaches a qualifying cancellation must solve exactly as it did before.
  EXPECT_EQ(result.restarts, Index{0});

  const BiCGSTAB cpu(strictSettings());
  const SolverResult cpuResult = cpu.solve(system);
  EXPECT_EQ(cpuResult.restarts, Index{0});
}

TEST(GpuBiCGSTABRestartTest, BreakdownIsStillReachable) {
  skipIfNoDevice();
  // A purely skew-symmetric system: (t, s) is identically zero from the very
  // first iteration, before any progress has been made, so the restart is
  // correctly REFUSED -- a restart from a sequence's own starting residual
  // cannot cure anything and would be an unbounded retry. Breakdown keeps its
  // meaning.
  SparseMatrixBuilder builder(4, 4);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, -1.0);
  builder.add(2, 3, 1.0);
  builder.add(3, 2, -1.0);
  const LinearSystem system(builder.build(), makeVector(4, 1.0));

  const auto gpu = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(gpu, nullptr);
  const SolverResult result = gpu->solve(system);

  const BiCGSTAB cpu(strictSettings());
  const SolverResult cpuResult = cpu.solve(system);

  EXPECT_EQ(cpuResult.status, SolverStatus::Breakdown);
  EXPECT_EQ(result.status, SolverStatus::Breakdown);
  EXPECT_EQ(result.restarts, Index{0});
}

TEST(GpuBiCGSTABRestartTest, RestartingSolveIsDeterministic) {
  skipIfNoDevice();
  const LinearSystem system = makeRestartingSystem();

  const auto gpu = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(gpu, nullptr);

  const SolverResult first = gpu->solve(system);
  ASSERT_GE(first.restarts, Index{1}) << "this system no longer restarts; determinism is being "
                                         "checked on a path that never restarts";

  for (int repeat = 0; repeat < 3; ++repeat) {
    const SolverResult again = gpu->solve(system);
    EXPECT_EQ(again.status, first.status);
    EXPECT_EQ(again.iterations, first.iterations);
    EXPECT_EQ(again.restarts, first.restarts);
    ASSERT_EQ(again.solution.size(), first.solution.size());
    for (Index i = 0; i < first.solution.size(); ++i) {
      // Bitwise, not "within tolerance": a restart recomputes b - A x on the
      // device, and a nondeterministic reduction there would show up here.
      EXPECT_EQ(again.solution[i], first.solution[i]) << "component " << i;
    }
  }
}

TEST(GpuBiCGSTABRestartTest, RestartAllocatesNothingInSteadyState) {
  skipIfNoDevice();
  const LinearSystem system = makeRestartingSystem();

  const auto gpu = cfd::gpu::makeGpuBiCGSTAB(strictSettings());
  ASSERT_NE(gpu, nullptr);

  const SolverResult first = gpu->solve(system);  // warms the persistent buffers
  ASSERT_GE(first.restarts, Index{1})
      << "this system no longer restarts; the allocation gate would not cover the restart path";

  cfd::gpu::resetGpuExecutionStats();
  constexpr int kRepeats = 4;
  for (int i = 0; i < kRepeats; ++i) {
    const SolverResult result = gpu->solve(system);
    EXPECT_GE(result.restarts, Index{1});
  }

  // The restart reuses v_ as its spmv target and zeroes the workspace vectors
  // in place; it must not introduce a scratch buffer of its own.
  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
}
