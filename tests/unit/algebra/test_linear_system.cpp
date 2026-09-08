#include <gtest/gtest.h>

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/core/Exception.hpp"

using cfd::algebra::LinearSystem;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

SparseMatrix identity2x2() {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 1.0);
  builder.add(1, 1, 1.0);
  return builder.build();
}

}  // namespace

TEST(LinearSystemTest, ValidSystemConstructsAndExposesData) {
  const LinearSystem system(identity2x2(), Vector{1.0, 2.0});
  EXPECT_EQ(system.size(), 2U);
  EXPECT_DOUBLE_EQ(system.rhs()[0], 1.0);
  EXPECT_EQ(system.matrix().rows(), 2U);
}

TEST(LinearSystemTest, RejectsNonSquareMatrix) {
  SparseMatrixBuilder builder(2, 3);
  builder.add(0, 0, 1.0);
  builder.add(1, 1, 1.0);
  EXPECT_THROW((LinearSystem(builder.build(), Vector(2, 0.0))), cfd::InvalidArgumentError);
}

TEST(LinearSystemTest, RejectsRhsSizeMismatch) {
  EXPECT_THROW((LinearSystem(identity2x2(), Vector(3, 0.0))), cfd::InvalidArgumentError);
}
