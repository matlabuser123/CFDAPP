#include <gtest/gtest.h>

#include <limits>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"

using cfd::Index;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::CG;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

SparseMatrix makeIdentity(Index n) {
  SparseMatrixBuilder builder(n, n);
  for (Index i = 0; i < n; ++i) {
    builder.add(i, i, 1.0);
  }
  return builder.build();
}

}  // namespace

TEST(SolverFailureTest, NaNMatrixRejectedAtConstruction) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((SparseMatrix(1, 1, {nan}, {0}, {0, 1})), cfd::InvalidArgumentError);
}

TEST(SolverFailureTest, InfMatrixRejectedAtConstruction) {
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_THROW((SparseMatrix(1, 1, {inf}, {0}, {0, 1})), cfd::InvalidArgumentError);
}

TEST(SolverFailureTest, CgFailsBeforeIteratingOnNanRhs) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const LinearSystem system(makeIdentity(3), Vector{1.0, nan, 3.0});

  const CG solver((LinearSolverSettings{}));
  const auto result = solver.solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::NonFiniteInput);
  EXPECT_EQ(result.iterations, 0U);
}

TEST(SolverFailureTest, BiCGSTABFailsBeforeIteratingOnNanRhs) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const LinearSystem system(makeIdentity(3), Vector{1.0, nan, 3.0});

  const BiCGSTAB solver((LinearSolverSettings{}));
  const auto result = solver.solve(system);

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::NonFiniteInput);
  EXPECT_EQ(result.iterations, 0U);
}

TEST(SolverFailureTest, InvalidInitialGuessSizeFailsCleanly) {
  const LinearSystem system(makeIdentity(3), Vector(3, 1.0));
  const CG solver((LinearSolverSettings{}));

  const auto result = solver.solve(system, Vector(2, 0.0));  // wrong size, no OOB indexing

  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.status, SolverStatus::NonFiniteInput);
}

TEST(SolverFailureTest, LinearSolverSettingsRejectsInvalidValues) {
  LinearSolverSettings negativeAbsolute;
  negativeAbsolute.absoluteTolerance = -1.0;
  EXPECT_THROW((CG(negativeAbsolute)), cfd::InvalidArgumentError);

  LinearSolverSettings zeroIterations;
  zeroIterations.maxIterations = 0;
  EXPECT_THROW((CG(zeroIterations)), cfd::InvalidArgumentError);

  LinearSolverSettings bothZeroTolerance;
  bothZeroTolerance.absoluteTolerance = 0.0;
  bothZeroTolerance.relativeTolerance = 0.0;
  EXPECT_THROW((CG(bothZeroTolerance)), cfd::InvalidArgumentError);
}
