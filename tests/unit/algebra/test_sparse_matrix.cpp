#include <gtest/gtest.h>

#include <limits>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

// [4 1 0
//  1 3 2
//  0 2 5]
SparseMatrix makeSampleMatrix() {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 1.0);
  builder.add(1, 1, 3.0);
  builder.add(1, 2, 2.0);
  builder.add(2, 1, 2.0);
  builder.add(2, 2, 5.0);
  return builder.build();
}

}  // namespace

TEST(SparseMatrixTest, BuilderProducesExpectedCsr) {
  const SparseMatrix matrix = makeSampleMatrix();
  EXPECT_EQ(matrix.rows(), 3U);
  EXPECT_EQ(matrix.columns(), 3U);
  EXPECT_EQ(matrix.nonZeros(), 7U);
}

TEST(SparseMatrixTest, BuilderSumsDuplicateContributions) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 1.0);
  builder.add(0, 0, 3.0);  // must sum to 4
  builder.add(0, 1, 2.0);
  builder.add(1, 0, 5.0);
  builder.add(1, 1, 6.0);

  const SparseMatrix matrix = builder.build();
  const Vector x{1.0, 1.0};
  const Vector y = matrix.multiply(x);
  EXPECT_DOUBLE_EQ(y[0], 6.0);   // 4*1 + 2*1
  EXPECT_DOUBLE_EQ(y[1], 11.0);  // 5*1 + 6*1
}

TEST(SparseMatrixTest, BuilderDropsExactZeroSums) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 5.0);
  builder.add(0, 1, 3.0);
  builder.add(0, 1, -3.0);  // sums to exactly zero -> dropped
  builder.add(1, 1, 1.0);

  const SparseMatrix matrix = builder.build();
  EXPECT_EQ(matrix.nonZeros(), 2U);  // (0,0) and (1,1) only
}

TEST(SparseMatrixTest, MatrixVectorMultiplicationKnownResult) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, 1.0);
  builder.add(1, 0, 1.0);
  builder.add(1, 1, 3.0);
  const SparseMatrix matrix = builder.build();

  const Vector x{1.0, 2.0};
  const Vector y = matrix.multiply(x);
  EXPECT_DOUBLE_EQ(y[0], 6.0);
  EXPECT_DOUBLE_EQ(y[1], 7.0);
}

TEST(SparseMatrixTest, MultiplyRejectsSizeMismatch) {
  const SparseMatrix matrix = makeSampleMatrix();
  const Vector wrongSize{1.0, 2.0};
  EXPECT_THROW((void)matrix.multiply(wrongSize), cfd::InvalidArgumentError);
}

TEST(SparseMatrixTest, DiagonalLookup) {
  const SparseMatrix matrix = makeSampleMatrix();
  EXPECT_DOUBLE_EQ(matrix.diagonal(0), 4.0);
  EXPECT_DOUBLE_EQ(matrix.diagonal(1), 3.0);
  EXPECT_DOUBLE_EQ(matrix.diagonal(2), 5.0);
}

TEST(SparseMatrixTest, DiagonalLookupThrowsWhenMissing) {
  // Row 0 has only column 1 -- no diagonal entry stored.
  const SparseMatrix matrix(2, 2, {1.0, 1.0, 2.0}, {1, 0, 1}, {0, 1, 3});
  EXPECT_THROW((void)matrix.diagonal(0), cfd::InvalidArgumentError);
  EXPECT_DOUBLE_EQ(matrix.diagonal(1), 2.0);
}

TEST(SparseMatrixTest, AllFinite) {
  const SparseMatrix matrix = makeSampleMatrix();
  EXPECT_TRUE(matrix.allFinite());
}

// --- Malformed CSR rejection ---------------------------------------------

TEST(SparseMatrixTest, RejectsWrongRowOffsetsSize) {
  EXPECT_THROW((SparseMatrix(2, 2, {1.0}, {0}, {0, 1})), cfd::InvalidArgumentError);
}

TEST(SparseMatrixTest, RejectsMismatchedValuesAndColumns) {
  EXPECT_THROW((SparseMatrix(1, 2, {1.0, 2.0}, {0}, {0, 2})), cfd::InvalidArgumentError);
}

TEST(SparseMatrixTest, RejectsNonZeroFirstRowOffset) {
  EXPECT_THROW((SparseMatrix(1, 1, {1.0}, {0}, {1, 1})), cfd::InvalidArgumentError);
}

TEST(SparseMatrixTest, RejectsRowOffsetsBackMismatch) {
  EXPECT_THROW((SparseMatrix(1, 1, {1.0}, {0}, {0, 2})), cfd::InvalidArgumentError);
}

TEST(SparseMatrixTest, RejectsOutOfRangeColumn) {
  EXPECT_THROW((SparseMatrix(1, 1, {1.0}, {5}, {0, 1})), cfd::InvalidArgumentError);
}

TEST(SparseMatrixTest, RejectsUnsortedColumnsWithinRow) {
  EXPECT_THROW((SparseMatrix(1, 3, {1.0, 1.0}, {2, 0}, {0, 2})), cfd::InvalidArgumentError);
}

TEST(SparseMatrixTest, RejectsNonFiniteValues) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((SparseMatrix(1, 1, {nan}, {0}, {0, 1})), cfd::InvalidArgumentError);
}
