// P4 -- Performance, sections 35-36: CPU vs GPU SpMV timing, separating
// kernel execution from host<->device transfer (this simple wrapper
// currently pays a fresh malloc+transfer every call -- see this file's
// own printed caveat) and reporting the crossover grid size where GPU
// total time first beats CPU total time. Only meaningful when built with
// CFDAPP_ENABLE_CUDA=ON.
#include <iostream>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaSpmv.hpp"
#include "cfd/gpu/GPUBackend.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;

namespace {

SparseMatrix makeGridMatrix(Index nx, Index ny) {
  const Index n = nx * ny;
  SparseMatrixBuilder builder(n, n);
  builder.reserve(5 * n);
  const auto index = [&](Index i, Index j) { return j * nx + i; };
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Index p = index(i, j);
      Real diagonal = 0.1;
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
      builder.add(p, p, diagonal);
    }
  }
  return builder.build();
}

}  // namespace

int main() {
  if (!cfd::gpu::cudaAvailable()) {
    std::cout << "cfd_benchmark_gpu_spmv: no usable CUDA device at runtime -- nothing to "
                "benchmark, exiting.\n";
    return 0;
  }

  std::cout << "cfd_benchmark_gpu_spmv -- end-to-end GPU time includes this wrapper's own fresh "
              "cudaMalloc+H2D-transfer+kernel+D2H-transfer *every call* (no persistent device "
              "residency yet -- section 31's own deferred optimization, see TODO.md's own P4 "
              "status note).\n";
  std::cout << "grid      | n      | cpu_seconds(30 reps) | gpu_seconds(30 reps, incl. "
              "transfer) | speedup\n";

  const std::vector<std::pair<Index, Index>> grids = {
      {20, 20}, {50, 50}, {100, 100}, {200, 200}, {400, 400}, {800, 800}};
  constexpr int kRepeats = 30;

  for (const auto& [nx, ny] : grids) {
    const SparseMatrix matrix = makeGridMatrix(nx, ny);
    Vector x(matrix.columns());
    for (Index i = 0; i < x.size(); ++i) x[i] = static_cast<Real>(i % 13) * 0.05;

    cfd::Timer cpuTimer;
    for (int r = 0; r < kRepeats; ++r) {
      const Vector y = matrix.multiply(x);
      (void)y;
    }
    const double cpuSeconds = cpuTimer.elapsedSeconds();

    cfd::Timer gpuTimer;
    for (int r = 0; r < kRepeats; ++r) {
      const Vector y = cfd::gpu::csrSpmvCuda(matrix, x);
      (void)y;
    }
    const double gpuSeconds = gpuTimer.elapsedSeconds();

    std::cout << nx << "x" << ny << "  | " << matrix.rows() << " | " << cpuSeconds << " | "
              << gpuSeconds << " | " << (cpuSeconds / gpuSeconds) << "\n";
  }

  return 0;
}
