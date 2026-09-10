// P4 -- Performance, sections 33-34: GPU SpMV correctness against the
// authoritative CPU implementation (cfd::algebra::SparseMatrix::multiply).
// This translation unit is only compiled/linked when CFDAPP_ENABLE_CUDA=ON
// (see tests/unit/CMakeLists.txt's own guard) -- a CPU-only build never
// even sees this file, let alone requires a GPU to pass its test suite
// (section 29/56).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/CudaSpmv.hpp"
#include "cfd/gpu/GPUBackend.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

// A small, deterministic, hand-constructed CSR matrix.
SparseMatrix makeSmallMatrix() {
  SparseMatrixBuilder builder(4, 4);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, -1.0);
  builder.add(1, 0, -1.0);
  builder.add(1, 1, 4.0);
  builder.add(1, 2, -1.0);
  builder.add(2, 1, -1.0);
  builder.add(2, 2, 4.0);
  builder.add(2, 3, -1.0);
  builder.add(3, 2, -1.0);
  builder.add(3, 3, 4.0);
  return builder.build();
}

// A larger, representative CSR matrix (a genuine 2D 5-point-stencil
// Laplacian-like sparsity pattern), the same shape momentum/pressure
// assembly actually produces.
SparseMatrix makeGridMatrix(Index nx, Index ny) {
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
      builder.add(p, p, diagonal + 0.1);  // strictly diagonally dominant.
    }
  }
  return builder.build();
}

Vector makeVector(Index n, Real seed) {
  Vector v(n);
  for (Index i = 0; i < n; ++i) v[i] = std::sin(seed * static_cast<Real>(i + 1));
  return v;
}

}  // namespace

TEST(CudaSpmvTest, CudaAvailableDoesNotThrow) {
  // Whatever the actual runtime answer is (true on this project's own
  // development GPU, potentially false in a CI container with no GPU
  // passthrough), the query itself must never throw.
  EXPECT_NO_THROW((void)cfd::gpu::cudaAvailable());
}

TEST(CudaSpmvTest, MatchesCpuReferenceOnASmallMatrix) {
  if (!cfd::gpu::cudaAvailable()) {
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
  const SparseMatrix matrix = makeSmallMatrix();
  const Vector x{1.0, 2.0, 3.0, 4.0};
  const Vector cpu = matrix.multiply(x);
  const Vector gpu = cfd::gpu::csrSpmvCuda(matrix, x);
  ASSERT_EQ(gpu.size(), cpu.size());
  for (Index i = 0; i < cpu.size(); ++i) {
    EXPECT_NEAR(gpu[i], cpu[i], 1e-9) << "row " << i;
  }
}

TEST(CudaSpmvTest, MatchesCpuReferenceOnARepresentativeGridMatrix) {
  if (!cfd::gpu::cudaAvailable()) {
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
  const SparseMatrix matrix = makeGridMatrix(50, 50);
  const Vector x = makeVector(matrix.columns(), 0.37);
  const Vector cpu = matrix.multiply(x);
  const Vector gpu = cfd::gpu::csrSpmvCuda(matrix, x);
  ASSERT_EQ(gpu.size(), cpu.size());
  Real maxAbsError = 0.0, maxRelError = 0.0;
  for (Index i = 0; i < cpu.size(); ++i) {
    const Real absError = std::abs(gpu[i] - cpu[i]);
    maxAbsError = std::max(maxAbsError, absError);
    if (std::abs(cpu[i]) > 1e-6) {
      maxRelError = std::max(maxRelError, absError / std::abs(cpu[i]));
    }
  }
  EXPECT_LT(maxAbsError, 1e-9);
  EXPECT_LT(maxRelError, 1e-9);
}

TEST(CudaSpmvTest, MismatchedVectorSizeThrows) {
  if (!cfd::gpu::cudaAvailable()) {
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
  const SparseMatrix matrix = makeSmallMatrix();
  const Vector wrongSize(matrix.columns() + 1, 1.0);
  EXPECT_THROW((void)cfd::gpu::csrSpmvCuda(matrix, wrongSize), InvalidArgumentError);
}

TEST(CudaSpmvTest, RepeatedCallsAreBitIdentical) {
  if (!cfd::gpu::cudaAvailable()) {
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
  const SparseMatrix matrix = makeGridMatrix(20, 20);
  const Vector x = makeVector(matrix.columns(), 1.1);
  const Vector first = cfd::gpu::csrSpmvCuda(matrix, x);
  for (int i = 0; i < 5; ++i) {
    const Vector repeat = cfd::gpu::csrSpmvCuda(matrix, x);
    for (Index row = 0; row < first.size(); ++row) {
      EXPECT_EQ(repeat[row], first[row]) << "row " << row << " iteration " << i;
    }
  }
}
