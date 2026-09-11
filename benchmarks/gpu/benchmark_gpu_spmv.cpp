// P4 -- Performance, sections 35-36: CPU vs GPU SpMV timing, separating
// kernel execution from host<->device transfer and reporting the
// crossover grid size where GPU total time first beats CPU total time.
//
// P6-PERF-001 -- Performance: now reports two GPU numbers side by side --
// "stateless" (cfd::gpu::csrSpmvCuda(), a fresh alloc+upload+download
// every call, kept for comparison) and "persistent" (a DeviceCsrMatrix/
// DeviceVector uploaded once outside the timed loop, then only spmv()
// inside it) -- plus GPUExecutionStats deltas, so the transfer-volume
// reduction this task delivers is directly visible in the output, not
// just inferred from wall time. Only meaningful when built with
// CFDAPP_ENABLE_CUDA=ON.
#include <cstdint>
#include <iostream>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaSpmv.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceVector.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

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

  std::cout << "cfd_benchmark_gpu_spmv -- 'stateless' is cfd::gpu::csrSpmvCuda(), a fresh "
              "cudaMalloc+H2D-transfer+kernel+D2H-transfer *every call*; 'persistent' uploads "
              "the matrix/vector once outside the timed loop and only launches the kernel "
              "inside it (P6-PERF-001).\n";
  std::cout << "grid      | n      | cpu_s(30) | gpu_stateless_s(30) | gpu_persistent_s(30) | "
              "speedup_stateless | speedup_persistent | stateless_h2d_calls | "
              "persistent_h2d_calls\n";

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

    cfd::gpu::resetGpuExecutionStats();
    cfd::Timer statelessTimer;
    for (int r = 0; r < kRepeats; ++r) {
      const Vector y = cfd::gpu::csrSpmvCuda(matrix, x);
      (void)y;
    }
    const double statelessSeconds = statelessTimer.elapsedSeconds();
    const std::uint64_t statelessH2dCalls = cfd::gpu::gpuExecutionStats().hostToDeviceCalls;

    // Persistent path: upload once, then only launch the kernel per
    // repeat -- no allocation or transfer inside the timed loop.
    cfd::gpu::DeviceCsrMatrix deviceMatrix;
    deviceMatrix.uploadStructureAndValues(matrix);
    cfd::gpu::DeviceVector deviceX;
    deviceX.uploadFrom(x);
    cfd::gpu::DeviceVector deviceY(matrix.rows());

    cfd::gpu::resetGpuExecutionStats();
    cfd::Timer persistentTimer;
    for (int r = 0; r < kRepeats; ++r) {
      cfd::gpu::spmv(deviceMatrix, deviceX, deviceY);
    }
    const double persistentSeconds = persistentTimer.elapsedSeconds();
    const std::uint64_t persistentH2dCalls = cfd::gpu::gpuExecutionStats().hostToDeviceCalls;
    const Vector finalResult = deviceY.downloadToVector();
    (void)finalResult;

    std::cout << nx << "x" << ny << "  | " << matrix.rows() << " | " << cpuSeconds << " | "
              << statelessSeconds << " | " << persistentSeconds << " | "
              << (cpuSeconds / statelessSeconds) << " | " << (cpuSeconds / persistentSeconds)
              << " | " << statelessH2dCalls << " | " << persistentH2dCalls << "\n";
  }

  // P6-GPU-001 -- Performance: the validation-gate's own required
  // evidence shape -- full GPUExecutionStats (allocations, reallocations,
  // H2D/D2H calls+bytes, upload/download/kernel time) for two consecutive
  // persistent-path operations on the same DeviceCsrMatrix/DeviceVector,
  // proving operation 2 reuses everything operation 1 allocated.
  {
    std::cout << "\npersistent-path reuse evidence (300x300 grid, structure/vector uploaded "
                "once):\n";
    const SparseMatrix matrix = makeGridMatrix(300, 300);
    Vector x(matrix.columns());
    for (Index i = 0; i < x.size(); ++i) x[i] = static_cast<Real>(i % 13) * 0.05;

    cfd::gpu::DeviceCsrMatrix deviceMatrix;
    deviceMatrix.uploadStructureAndValues(matrix);
    cfd::gpu::DeviceVector deviceX;
    deviceX.uploadFrom(x);
    cfd::gpu::DeviceVector deviceY(matrix.rows());

    const auto printStats = [](const char* label) {
      const auto& stats = cfd::gpu::gpuExecutionStats();
      std::cout << label << "\n"
                << "  allocations:      " << stats.allocations << "\n"
                << "  reallocations:    " << stats.reallocations << "\n"
                << "  H2D transfers:    " << stats.hostToDeviceCalls << "\n"
                << "  H2D bytes:        " << stats.hostToDeviceBytes << "\n"
                << "  D2H transfers:    " << stats.deviceToHostCalls << "\n"
                << "  D2H bytes:        " << stats.deviceToHostBytes << "\n"
                << "  upload time (s):  " << stats.uploadSeconds << "\n"
                << "  download time(s): " << stats.downloadSeconds << "\n"
                << "  kernel time (s):  " << stats.kernelSeconds << "\n";
    };

    cfd::gpu::resetGpuExecutionStats();
    cfd::gpu::spmv(deviceMatrix, deviceX, deviceY);
    printStats("Operation 1 (first persistent-path spmv() after setup):");

    cfd::gpu::resetGpuExecutionStats();
    cfd::gpu::spmv(deviceMatrix, deviceX, deviceY);
    printStats("Operation 2 (second call, same matrix/vectors, no re-upload):");
  }

  return 0;
}
