#include <gtest/gtest.h>

#include <memory>

#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::CG;
using cfd::algebra::JacobiPreconditioner;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
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

// Independently recomputes ||b - A x|| rather than trusting the solver's
// own bookkeeping.
void verifyResidualIndependently(const LinearSystem& system, const Vector& x, Real tolerance) {
  const Vector residual = system.rhs() - system.matrix().multiply(x);
  EXPECT_LE(cfd::algebra::l2Norm(residual), tolerance);
}

}  // namespace

TEST(CGTest, SolvesTwoByTwoSpdSystem) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 1.0);
  builder.add(1, 1, 3.0);
  const LinearSystem system(builder.build(), Vector{6.0, 7.0});

  const CG solver(strictSettings());
  const auto result = solver.solve(system);

  EXPECT_TRUE(result.converged());
  EXPECT_GT(result.iterations, 0U);
  EXPECT_NEAR(result.solution[0], 1.0, 1e-10);
  EXPECT_NEAR(result.solution[1], 2.0, 1e-10);
  verifyResidualIndependently(system, result.solution, 1e-9);
}

TEST(CGTest, SolvesThreeByThreeSpdSystem) {
  // A = [4 -1 0; -1 4 -1; 0 -1 3], x_true = [1,2,3] -> b = [2,4,7]
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, -1.0);
  builder.add(1, 0, -1.0);
  builder.add(1, 1, 4.0);
  builder.add(1, 2, -1.0);
  builder.add(2, 1, -1.0);
  builder.add(2, 2, 3.0);
  const LinearSystem system(builder.build(), Vector{2.0, 4.0, 7.0});

  const CG solver(strictSettings());
  const auto result = solver.solve(system);

  EXPECT_TRUE(result.converged());
  EXPECT_NEAR(result.solution[0], 1.0, 1e-9);
  EXPECT_NEAR(result.solution[1], 2.0, 1e-9);
  EXPECT_NEAR(result.solution[2], 3.0, 1e-9);
  verifyResidualIndependently(system, result.solution, 1e-8);
}

TEST(CGTest, IdentityMatrixSolvesImmediately) {
  SparseMatrixBuilder builder(4, 4);
  for (Index i = 0; i < 4; ++i) {
    builder.add(i, i, 1.0);
  }
  const Vector b{1.0, -2.0, 3.5, 10.0};
  const LinearSystem system(builder.build(), b);

  const CG solver(strictSettings());
  const auto result = solver.solve(system);

  EXPECT_TRUE(result.converged());
  for (Index i = 0; i < 4; ++i) {
    EXPECT_NEAR(result.solution[i], b[i], 1e-10);
  }
}

TEST(CGTest, DiagonalMatrixWithJacobiPreconditioner) {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 2.0);
  builder.add(1, 1, 4.0);
  builder.add(2, 2, 8.0);
  const LinearSystem system(builder.build(), Vector{2.0, 8.0, 24.0});

  const CG solver(strictSettings(), std::make_shared<JacobiPreconditioner>());
  const auto result = solver.solve(system);

  EXPECT_TRUE(result.converged());
  EXPECT_NEAR(result.solution[0], 1.0, 1e-10);
  EXPECT_NEAR(result.solution[1], 2.0, 1e-10);
  EXPECT_NEAR(result.solution[2], 3.0, 1e-10);
}

TEST(CGTest, RepeatedSolveRebuildsPreconditionerEachTime) {
  // P6-GPU-003: preconditioner_->build(A) must run again on a second
  // solve() call against a *different* matrix on the same solver
  // instance -- proving the preconditioner tracks the matrix passed to
  // solve(), not a value captured once at construction.
  SparseMatrixBuilder builderA(2, 2);
  builderA.add(0, 0, 2.0);
  builderA.add(1, 1, 4.0);
  const LinearSystem systemA(builderA.build(), Vector{2.0, 8.0});

  const auto preconditioner = std::make_shared<JacobiPreconditioner>();
  const CG solver(strictSettings(), preconditioner);
  const auto resultA = solver.solve(systemA);
  EXPECT_TRUE(resultA.converged());
  EXPECT_NEAR(resultA.solution[0], 1.0, 1e-10);
  EXPECT_NEAR(resultA.solution[1], 2.0, 1e-10);

  SparseMatrixBuilder builderB(2, 2);
  builderB.add(0, 0, 5.0);
  builderB.add(1, 1, 10.0);
  const LinearSystem systemB(builderB.build(), Vector{10.0, 30.0});
  const auto resultB = solver.solve(systemB);
  EXPECT_TRUE(resultB.converged());
  EXPECT_NEAR(resultB.solution[0], 2.0, 1e-10);
  EXPECT_NEAR(resultB.solution[1], 3.0, 1e-10);
}

TEST(CGTest, InvalidSystemWhenPreconditionerRejectsTheMatrix) {
  // P6-GPU-003: a preconditioner that throws out of build() (e.g. Jacobi
  // against a zero-diagonal matrix) must surface as
  // SolverStatus::InvalidSystem, not an exception escaping solve() --
  // SIMPLE::solve() does not wrap momentumSolver->solve()/
  // pressureSolver->solve() in a try/catch, so an uncaught exception here
  // would crash the outer SIMPLE loop instead of reporting a clean
  // failure status.
  const SparseMatrix matrix(2, 2, {0.0, 1.0, 1.0, 2.0}, {0, 1, 0, 1}, {0, 2, 4});
  const LinearSystem system(matrix, Vector{1.0, 1.0});

  const CG solver(strictSettings(), std::make_shared<JacobiPreconditioner>());
  const auto result = solver.solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::InvalidSystem);
}

TEST(CGTest, ZeroRhsConvergesImmediately) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 1.0);
  builder.add(1, 1, 3.0);
  const LinearSystem system(builder.build(), Vector(2, 0.0));

  const CG solver(strictSettings());
  const auto result = solver.solve(system, Vector(2, 0.0));

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.iterations, 0U);
  EXPECT_DOUBLE_EQ(result.finalResidual, 0.0);
  EXPECT_DOUBLE_EQ(result.solution[0], 0.0);
  EXPECT_DOUBLE_EQ(result.solution[1], 0.0);
}

TEST(CGTest, ExactInitialGuessConvergesAtIterationZero) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 1.0);
  builder.add(1, 1, 3.0);
  const LinearSystem system(builder.build(), Vector{6.0, 7.0});

  const CG solver(strictSettings());
  const auto result = solver.solve(system, Vector{1.0, 2.0});

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.iterations, 0U);
}

TEST(CGTest, MaxIterationsReportsFailureNotSuccess) {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, -1.0);
  builder.add(1, 0, -1.0);
  builder.add(1, 1, 4.0);
  builder.add(1, 2, -1.0);
  builder.add(2, 1, -1.0);
  builder.add(2, 2, 3.0);
  const LinearSystem system(builder.build(), Vector{2.0, 4.0, 7.0});

  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-14;
  settings.relativeTolerance = 1e-14;
  settings.maxIterations = 1;

  const CG solver(settings);
  const auto result = solver.solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::MaxIterations);
  EXPECT_EQ(result.iterations, 1U);
}
