#include <gtest/gtest.h>

#include <cstddef>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/CG.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"

using cfd::algebra::BiCGSTAB;
using cfd::algebra::CG;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
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

}  // namespace

TEST(SolverDeterminismTest, CGRepeatedSolvesAreIdentical) {
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
  const auto reference = solver.solve(system);

  for (int trial = 0; trial < 20; ++trial) {
    const auto repeat = solver.solve(system);
    EXPECT_EQ(repeat.status, reference.status);
    EXPECT_EQ(repeat.iterations, reference.iterations);
    ASSERT_EQ(repeat.residualHistory.size(), reference.residualHistory.size());
    for (std::size_t i = 0; i < repeat.residualHistory.size(); ++i) {
      EXPECT_EQ(repeat.residualHistory[i], reference.residualHistory[i]);
    }
    for (Vector::size_type i = 0; i < repeat.solution.size(); ++i) {
      EXPECT_EQ(repeat.solution[i], reference.solution[i]);
    }
  }
}

TEST(SolverDeterminismTest, BiCGSTABRepeatedSolvesAreIdentical) {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 2.0);
  builder.add(1, 1, 3.0);
  builder.add(1, 2, 1.0);
  builder.add(2, 1, 1.0);
  builder.add(2, 2, 2.0);
  const LinearSystem system(builder.build(), Vector{6.0, 11.0, 8.0});

  const BiCGSTAB solver(strictSettings());
  const auto reference = solver.solve(system);

  for (int trial = 0; trial < 20; ++trial) {
    const auto repeat = solver.solve(system);
    EXPECT_EQ(repeat.status, reference.status);
    EXPECT_EQ(repeat.iterations, reference.iterations);
    for (Vector::size_type i = 0; i < repeat.solution.size(); ++i) {
      EXPECT_EQ(repeat.solution[i], reference.solution[i]);
    }
  }
}
