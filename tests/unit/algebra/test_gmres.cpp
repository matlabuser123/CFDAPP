// P12-NUM-004: restarted GMRES(m) (cfd/algebra/GMRES.hpp) -- verification
// against exact solutions / independent CG solutions, restart and
// preconditioning behavior, and every reported failure status.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/GMRES.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::GMRES;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::LinearSystem;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

// 1D Dirichlet Laplacian: tridiag(-1, 2, -1) -- SPD.
SparseMatrix laplacian1D(Index n) {
  SparseMatrixBuilder builder(n, n);
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, 2.0);
    if (i > 0) builder.add(i, i - 1, -1.0);
    if (i + 1 < n) builder.add(i, i + 1, -1.0);
  }
  return builder.build();
}

// Upwinded 1D convection-diffusion: tridiag(-1 - c, 2 + c, -1) -- strongly
// non-symmetric for c > 0 (CG is not applicable).
SparseMatrix convectionDiffusion1D(Index n, Real c) {
  SparseMatrixBuilder builder(n, n);
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, 2.0 + c);
    if (i > 0) builder.add(i, i - 1, -1.0 - c);
    if (i + 1 < n) builder.add(i, i + 1, -1.0);
  }
  return builder.build();
}

Vector rhsFor(const SparseMatrix& a, const Vector& exact) { return a.multiply(exact); }

Vector exactSolution(Index n) {
  Vector x(n);
  for (Index i = 0; i < n; ++i)
    x[i] = std::sin(0.3 * static_cast<Real>(i)) + 0.1 * static_cast<Real>(i);
  return x;
}

Real maxError(const Vector& a, const Vector& b) {
  Real worst = 0.0;
  for (Index i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
  return worst;
}

LinearSolverSettings tight() {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-13;
  settings.relativeTolerance = 1e-12;
  settings.maxIterations = 500;
  return settings;
}

}  // namespace

TEST(GMRESTest, SolvesSPDSystemAndAgreesWithCG) {
  const Index n = 40;
  const SparseMatrix a = laplacian1D(n);
  const Vector exact = exactSolution(n);
  const LinearSystem system(a, rhsFor(a, exact));
  const auto gmres = GMRES(tight()).solve(system);
  const auto cg = cfd::algebra::CG(tight()).solve(system);
  ASSERT_EQ(gmres.status, SolverStatus::Converged);
  ASSERT_EQ(cg.status, SolverStatus::Converged);
  EXPECT_LT(maxError(gmres.solution, exact), 1e-9);
  EXPECT_LT(maxError(gmres.solution, cg.solution), 1e-9);
  EXPECT_LE(l2Norm(system.rhs() - a.multiply(gmres.solution)),
            1e-12 * gmres.initialResidual + 1e-13);
  EXPECT_EQ(gmres.residualHistory.size(), gmres.iterations + 1);
}

TEST(GMRESTest, SolvesNonSymmetricSystem) {
  const Index n = 50;
  const SparseMatrix a = convectionDiffusion1D(n, 5.0);
  const Vector exact = exactSolution(n);
  const LinearSystem system(a, rhsFor(a, exact));
  const auto result = GMRES(tight()).solve(system);
  ASSERT_EQ(result.status, SolverStatus::Converged);
  EXPECT_LT(maxError(result.solution, exact), 1e-9);
}

// Restart length 3 on a 30-unknown system: many restart cycles, still
// converges to the same solution, deterministically.
TEST(GMRESTest, RestartedSolveConvergesDeterministically) {
  const Index n = 30;
  const SparseMatrix a = convectionDiffusion1D(n, 1.0);
  const Vector exact = exactSolution(n);
  const LinearSystem system(a, rhsFor(a, exact));
  LinearSolverSettings settings = tight();
  settings.gmresRestart = 3;
  settings.maxIterations = 5000;
  const auto first = GMRES(settings).solve(system);
  const auto second = GMRES(settings).solve(system);
  ASSERT_EQ(first.status, SolverStatus::Converged);
  EXPECT_GT(first.iterations, 3u);  // restarted at least once
  EXPECT_LT(maxError(first.solution, exact), 1e-9);
  ASSERT_EQ(first.iterations, second.iterations);
  for (Index i = 0; i < n; ++i) EXPECT_EQ(first.solution[i], second.solution[i]);
}

TEST(GMRESTest, JacobiPreconditionedSolveMatches) {
  const Index n = 40;
  const SparseMatrix a = convectionDiffusion1D(n, 3.0);
  const Vector exact = exactSolution(n);
  const LinearSystem system(a, rhsFor(a, exact));
  LinearSolverSettings settings = tight();
  settings.type = LinearSolverType::GMRES;
  settings.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  const auto result = cfd::algebra::makeLinearSolver(settings)->solve(system);
  ASSERT_EQ(result.status, SolverStatus::Converged);
  EXPECT_LT(maxError(result.solution, exact), 1e-9);
}

TEST(GMRESTest, ZeroRightHandSideConvergesImmediately) {
  const LinearSystem system(laplacian1D(5), Vector(5, 0.0));
  const auto result = GMRES(tight()).solve(system);
  EXPECT_EQ(result.status, SolverStatus::Converged);
  EXPECT_EQ(result.iterations, 0u);
}

TEST(GMRESTest, NonFiniteInputIsRejectedBeforeIterating) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const LinearSystem system(laplacian1D(3), Vector{1.0, nan, 1.0});
  const auto result = GMRES(tight()).solve(system);
  EXPECT_EQ(result.status, SolverStatus::NonFiniteInput);
  EXPECT_EQ(result.iterations, 0u);
}

TEST(GMRESTest, IterationBudgetIsReportedAsMaxIterations) {
  const SparseMatrix a = laplacian1D(20);
  const LinearSystem system(a, rhsFor(a, exactSolution(20)));
  LinearSolverSettings settings = tight();
  settings.maxIterations = 2;
  const auto result = GMRES(settings).solve(system);
  EXPECT_EQ(result.status, SolverStatus::MaxIterations);
  EXPECT_EQ(result.iterations, 2u);
}

// A = [[1,1],[1,1]] (singular), b = [1,0] (inconsistent): the Krylov space
// is exhausted with a zero pivot in the least-squares system -- reported as
// Breakdown, never NaN.
TEST(GMRESTest, SingularInconsistentSystemReportsBreakdown) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 1.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 1.0);
  builder.add(1, 1, 1.0);
  const LinearSystem system(builder.build(), Vector{1.0, 0.0});
  const auto result = GMRES(tight()).solve(system);
  EXPECT_EQ(result.status, SolverStatus::Breakdown);
  EXPECT_TRUE(result.solution.allFinite());
}

TEST(GMRESTest, InvalidRestartLengthIsRejected) {
  LinearSolverSettings settings = tight();
  settings.gmresRestart = 0;
  EXPECT_THROW(GMRES{settings}, cfd::InvalidArgumentError);
}

// There is no GPU GMRES: a GPU request takes the documented, logged CPU
// fallback of makeLinearSolver and reports the CPU backend.
TEST(GMRESTest, GpuRequestFallsBackToCpu) {
  LinearSolverSettings settings = tight();
  settings.type = LinearSolverType::GMRES;
  settings.backend = cfd::algebra::LinearSolverBackend::GPU;
  const SparseMatrix a = laplacian1D(10);
  const LinearSystem system(a, rhsFor(a, exactSolution(10)));
  const auto result = cfd::algebra::makeLinearSolver(settings)->solve(system);
  EXPECT_EQ(result.status, SolverStatus::Converged);
  EXPECT_EQ(result.backendUsed, cfd::algebra::LinearSolverBackend::CPU);
}
