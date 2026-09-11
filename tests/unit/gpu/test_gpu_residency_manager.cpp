// P6-GPU-001 -- Performance: cfd::gpu::GpuResidencyManager's actual
// device-side behavior (structure-reuse, value-only updates, field
// mirroring) requires a real CUDA device to observe through
// GPUExecutionStats -- see test_device_buffer.cpp's own header comment
// for the GTEST_SKIP()-if-no-device convention this file follows.
// tests/unit/core/test_gpu_residency_manager.cpp covers the
// always-built, CUDA-independent CPU-fallback contract instead.
#include <gtest/gtest.h>

#include <cstdint>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuResidencyManager.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::fields::ScalarField;
using cfd::gpu::GpuResidencyManager;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) {
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
}

// The same 5-point-stencil shape used throughout this test suite --
// representative of a real momentum/pressure-correction matrix.
SparseMatrix makeGridMatrix(Index nx, Index ny, Real diagonalBoost) {
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
      builder.add(p, p, diagonal + diagonalBoost);
    }
  }
  return builder.build();
}

}  // namespace

TEST(GpuResidencyManagerTest, ActiveWithAUsableDevice) {
  skipIfNoDevice();
  const GpuResidencyManager manager;
  EXPECT_TRUE(manager.active());
}

TEST(GpuResidencyManagerTest, SyncMatrixUploadsStructureOnceThenValuesOnlyOnRepeatedCalls) {
  skipIfNoDevice();
  const Index n = 15;
  GpuResidencyManager manager;

  cfd::gpu::resetGpuExecutionStats();
  manager.syncMatrix("pressure", makeGridMatrix(n, n, 0.1));
  // First call for this key: rowOffsets + columnIndices + values == 3
  // host-to-device transfers (DeviceCsrMatrix::uploadStructureAndValues).
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().hostToDeviceCalls, 3u);

  cfd::gpu::resetGpuExecutionStats();
  // This is the direct proof P6-GPU-001 requires at the manager level:
  // repeated production-shaped calls with unchanged sparsity must not
  // re-upload structure, and must not allocate.
  constexpr int kIterations = 5;
  for (int i = 0; i < kIterations; ++i) {
    manager.syncMatrix("pressure", makeGridMatrix(n, n, 0.1 + static_cast<Real>(i)));
  }
  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.hostToDeviceCalls, static_cast<std::uint64_t>(kIterations));
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
}

TEST(GpuResidencyManagerTest, SyncMatrixHandlesAGenuineStructureChange) {
  skipIfNoDevice();
  GpuResidencyManager manager;
  manager.syncMatrix("pressure", makeGridMatrix(5, 5, 0.1));

  cfd::gpu::resetGpuExecutionStats();
  // A different grid size (e.g. a re-meshed case) must be re-uploaded in
  // full, not fed through the value-only path.
  EXPECT_NO_THROW(manager.syncMatrix("pressure", makeGridMatrix(8, 8, 0.1)));
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().hostToDeviceCalls, 3u);
}

TEST(GpuResidencyManagerTest, DistinctKeysAreTrackedIndependently) {
  skipIfNoDevice();
  GpuResidencyManager manager;

  cfd::gpu::resetGpuExecutionStats();
  manager.syncMatrix("momentum_u", makeGridMatrix(10, 10, 0.1));
  manager.syncMatrix("momentum_v", makeGridMatrix(10, 10, 0.2));
  manager.syncMatrix("pressure", makeGridMatrix(10, 10, 0.0));
  // Three distinct keys, each a first-time (structure+values) upload.
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().hostToDeviceCalls, 9u);

  cfd::gpu::resetGpuExecutionStats();
  manager.syncMatrix("momentum_u", makeGridMatrix(10, 10, 1.1));
  manager.syncMatrix("momentum_v", makeGridMatrix(10, 10, 1.2));
  manager.syncMatrix("pressure", makeGridMatrix(10, 10, 1.0));
  // Same three keys, same structure -- value-only updates, one call each.
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().hostToDeviceCalls, 3u);
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().allocations, 0u);
}

TEST(GpuResidencyManagerTest, SyncFieldReusesItsDeviceAllocationAcrossCalls) {
  skipIfNoDevice();
  GpuResidencyManager manager;
  const ScalarField field(500, 2.0);

  manager.syncField("pressure", field);
  cfd::gpu::resetGpuExecutionStats();
  for (int i = 0; i < 4; ++i) {
    manager.syncField("pressure", field);
  }
  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.hostToDeviceCalls, 4u);
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
}
