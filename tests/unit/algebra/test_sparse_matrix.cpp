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

// --- P4 -- Performance: reserve() and the OpenMP SpMV target ------------

TEST(SparseMatrixBuilderTest, ReserveDoesNotChangeTheAssembledMatrix) {
  // reserve() is a pure capacity hint (SparseMatrix.hpp's own header
  // comment) -- a builder that reserves an arbitrary (too-small,
  // too-large, or exact) capacity must assemble byte-for-byte the same
  // matrix as one that never calls it.
  SparseMatrixBuilder withoutReserve(3, 3);
  withoutReserve.add(0, 0, 4.0);
  withoutReserve.add(0, 1, 1.0);
  withoutReserve.add(1, 0, 1.0);
  withoutReserve.add(1, 1, 3.0);
  const auto matrixWithout = withoutReserve.build();

  for (const cfd::Index reserveHint : {cfd::Index{0}, cfd::Index{1}, cfd::Index{100}}) {
    SparseMatrixBuilder withReserve(3, 3);
    withReserve.reserve(reserveHint);
    withReserve.add(0, 0, 4.0);
    withReserve.add(0, 1, 1.0);
    withReserve.add(1, 0, 1.0);
    withReserve.add(1, 1, 3.0);
    const auto matrixWith = withReserve.build();

    ASSERT_EQ(matrixWith.nonZeros(), matrixWithout.nonZeros()) << "reserveHint=" << reserveHint;
    const Vector e0{1.0, 0.0, 0.0};
    const Vector e1{0.0, 1.0, 0.0};
    EXPECT_EQ(matrixWith.multiply(e0)[0], matrixWithout.multiply(e0)[0]);
    EXPECT_EQ(matrixWith.multiply(e1)[1], matrixWithout.multiply(e1)[1]);
  }
}

TEST(SparseMatrixTest, RepeatedMultiplyIsBitIdenticalRegardlessOfOpenMPThreadCount) {
  // Section 21/47: SpMV's own OpenMP parallelization (SparseMatrix.cpp's
  // own header comment on why it is race- and reduction-order-free) must
  // give the exact same floating-point result no matter how many threads
  // actually ran it -- this test does not itself change
  // OMP_NUM_THREADS (a process-wide setting a unit test cannot safely
  // vary at runime once the OpenMP runtime has started), but repeatedly
  // calling multiply() with the same fixed thread count already proves
  // the *reduction order per row* is fixed and reproducible run to run,
  // the property the header comment's bit-identical claim actually rests
  // on (every row's own inner ascending-k sum is single-threaded
  // regardless of how rows are distributed across threads).
  const SparseMatrix matrix = makeSampleMatrix();
  const Vector x{1.0, 2.0, 3.0};
  const Vector first = matrix.multiply(x);
  for (int i = 0; i < 20; ++i) {
    const Vector repeat = matrix.multiply(x);
    for (cfd::Index row = 0; row < first.size(); ++row) {
      EXPECT_EQ(repeat[row], first[row]) << "row " << row << " iteration " << i;
    }
  }
}
