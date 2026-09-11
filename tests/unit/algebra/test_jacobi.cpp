#include <gtest/gtest.h>

#include <limits>

#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"

using cfd::algebra::JacobiPreconditioner;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

TEST(JacobiTest, DiagonalMatrixExactInverse) {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 2.0);
  builder.add(1, 1, 4.0);
  builder.add(2, 2, 8.0);
  const SparseMatrix matrix = builder.build();

  JacobiPreconditioner jacobi;
  jacobi.build(matrix);

  const Vector r{2.0, 8.0, 24.0};
  Vector z(3);
  jacobi.apply(r, z);

  EXPECT_DOUBLE_EQ(z[0], 1.0);
  EXPECT_DOUBLE_EQ(z[1], 2.0);
  EXPECT_DOUBLE_EQ(z[2], 3.0);
}

TEST(JacobiTest, RejectsMissingDiagonal) {
  // Row 0 has only column 1 -- no diagonal entry stored.
  const SparseMatrix matrix(2, 2, {1.0, 1.0, 2.0}, {1, 0, 1}, {0, 1, 3});
  JacobiPreconditioner jacobi;
  EXPECT_THROW(jacobi.build(matrix), cfd::InvalidArgumentError);
}

TEST(JacobiTest, RejectsZeroDiagonal) {
  // [[0,1],[1,2]] with the zero explicitly stored (direct CSR
  // construction bypasses the builder, which would otherwise drop it).
  const SparseMatrix matrix(2, 2, {0.0, 1.0, 1.0, 2.0}, {0, 1, 0, 1}, {0, 2, 4});
  JacobiPreconditioner jacobi;
  EXPECT_THROW(jacobi.build(matrix), cfd::InvalidArgumentError);
}

TEST(JacobiTest, RejectsNearZeroDiagonal) {
  // P6-GPU-003: a diagonal that is finite and technically nonzero but
  // numerically dangerous to invert (1e-20, far below
  // cfd::constants::small = 1e-12) must be rejected the same way an
  // exact zero is -- inverting it would produce a preconditioner entry
  // on the order of 1e20, which would poison the whole Krylov iteration
  // rather than merely fail to help it.
  const SparseMatrix matrix(2, 2, {1.0e-20, 1.0, 1.0, 2.0}, {0, 1, 0, 1}, {0, 2, 4});
  JacobiPreconditioner jacobi;
  EXPECT_THROW(jacobi.build(matrix), cfd::InvalidArgumentError);
}

TEST(JacobiTest, AcceptsSmallButSafeDiagonal) {
  // A legitimately small (not "dangerous") diagonal entry -- e.g. a
  // fine-mesh, small-coefficient CFD discretization -- must not be
  // rejected just for being small; only |A_ii| < cfd::constants::small
  // (1e-12) is treated as dangerous.
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 1.0e-6);
  builder.add(1, 1, 4.0);
  const SparseMatrix matrix = builder.build();

  JacobiPreconditioner jacobi;
  EXPECT_NO_THROW(jacobi.build(matrix));

  const Vector r{2.0e-6, 8.0};
  Vector z(2);
  jacobi.apply(r, z);
  EXPECT_DOUBLE_EQ(z[0], 2.0);
  EXPECT_DOUBLE_EQ(z[1], 2.0);
}

TEST(JacobiTest, NonFiniteMatrixValuesAreUnreachableByJacobi) {
  // SparseMatrix itself rejects non-finite values at construction (see
  // SparseMatrixTest.RejectsNonFiniteValues), so a non-finite diagonal can
  // never actually reach JacobiPreconditioner::build through a normally
  // constructed SparseMatrix. JacobiPreconditioner::build still checks
  // isfinite() itself as defense in depth, but this test documents where
  // the guarantee actually comes from.
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((SparseMatrix(1, 1, {nan}, {0}, {0, 1})), cfd::InvalidArgumentError);
}
