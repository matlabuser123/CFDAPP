#include <gtest/gtest.h>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"

using cfd::algebra::BiCGSTAB;
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

// A = [4 1 0; 2 3 1; 0 1 2] (not symmetric), x_true = [1,2,3] -> b = [6,11,8]
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

}  // namespace

TEST(BiCGSTABTest, SolvesNonsymmetricSystem) {
  const LinearSystem system(makeNonsymmetricMatrix(), Vector{6.0, 11.0, 8.0});

  const BiCGSTAB solver(strictSettings());
  const auto result = solver.solve(system);

  EXPECT_TRUE(result.converged());
  EXPECT_NEAR(result.solution[0], 1.0, 1e-8);
  EXPECT_NEAR(result.solution[1], 2.0, 1e-8);
  EXPECT_NEAR(result.solution[2], 3.0, 1e-8);

  const Vector residual = system.rhs() - system.matrix().multiply(result.solution);
  EXPECT_LE(cfd::algebra::l2Norm(residual), 1e-7);
}

TEST(BiCGSTABTest, ZeroRhsConvergesImmediately) {
  const LinearSystem system(makeNonsymmetricMatrix(), Vector(3, 0.0));

  const BiCGSTAB solver(strictSettings());
  const auto result = solver.solve(system, Vector(3, 0.0));

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.iterations, 0U);
  EXPECT_DOUBLE_EQ(result.finalResidual, 0.0);
}

TEST(BiCGSTABTest, ExactInitialGuessConvergesAtIterationZero) {
  const LinearSystem system(makeNonsymmetricMatrix(), Vector{6.0, 11.0, 8.0});

  const BiCGSTAB solver(strictSettings());
  const auto result = solver.solve(system, Vector{1.0, 2.0, 3.0});

  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.iterations, 0U);
}

TEST(BiCGSTABTest, MaxIterationsReportsFailureNotSuccess) {
  const LinearSystem system(makeNonsymmetricMatrix(), Vector{6.0, 11.0, 8.0});

  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-14;
  settings.relativeTolerance = 1e-14;
  settings.maxIterations = 1;

  const BiCGSTAB solver(settings);
  const auto result = solver.solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::MaxIterations);
  EXPECT_EQ(result.iterations, 1U);
}
