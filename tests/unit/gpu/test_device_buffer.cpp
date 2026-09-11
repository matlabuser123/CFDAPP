// P6-PERF-001 -- Performance: the persistent-GPU-residency primitives
// (DeviceBuffer/DeviceVector/DeviceCsrMatrix/DeviceField) and the
// GPUExecutionStats instrumentation that proves they actually avoid
// repeated allocation/transfer. Only compiled when CFDAPP_ENABLE_CUDA=ON
// (tests/unit/gpu/CMakeLists.txt), same GTEST_SKIP()-if-no-device
// pattern as test_cuda_spmv.cpp so a CI container with no GPU passthrough
// still builds and "passes" (by skipping) rather than failing.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceField.hpp"
#include "cfd/gpu/DeviceVector.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::fields::Field;
using cfd::gpu::DeviceBuffer;
using cfd::gpu::DeviceCsrMatrix;
using cfd::gpu::DeviceField;
using cfd::gpu::DeviceVector;
using cfd::gpu::SyncState;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) {
    GTEST_SKIP() << "no usable CUDA device at runtime";
  }
}

// The same 5-point-stencil grid matrix shape test_cuda_spmv.cpp uses --
// representative of real momentum/pressure sparsity.
SparseMatrix makeGridMatrix(Index nx, Index ny, Real diagonalBoost = 0.1) {
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

Vector makeVector(Index n, Real seed) {
  Vector v(n);
  for (Index i = 0; i < n; ++i) v[i] = std::sin(seed * static_cast<Real>(i + 1));
  return v;
}

}  // namespace

// ---------------------------------------------------------------------
// DeviceBuffer<T>
// ---------------------------------------------------------------------

TEST(DeviceBufferTest, DefaultConstructedIsEmpty) {
  skipIfNoDevice();
  DeviceBuffer<Real> buffer;
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.size(), 0u);
  EXPECT_EQ(buffer.capacity(), 0u);
  EXPECT_EQ(buffer.data(), nullptr);
}

TEST(DeviceBufferTest, ZeroSizeResizeNeverAllocates) {
  skipIfNoDevice();
  cfd::gpu::resetGpuExecutionStats();
  DeviceBuffer<Real> buffer;
  buffer.resize(0);
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().allocations, 0u);
  EXPECT_EQ(buffer.data(), nullptr);
}

TEST(DeviceBufferTest, UploadDownloadRoundTrip) {
  skipIfNoDevice();
  const std::vector<Real> host = {1.0, 2.0, 3.0, 4.0, 5.0};
  DeviceBuffer<Real> buffer;
  buffer.uploadFrom(host.data(), host.size());
  ASSERT_EQ(buffer.size(), host.size());

  std::vector<Real> roundTrip(host.size());
  buffer.downloadTo(roundTrip.data(), roundTrip.size());
  EXPECT_EQ(roundTrip, host);
}

TEST(DeviceBufferTest, ResizeReusesCapacityWhenNotGrowingPastIt) {
  skipIfNoDevice();
  DeviceBuffer<Real> buffer;
  buffer.resize(100);
  const Index capacityAfterGrow = buffer.capacity();

  cfd::gpu::resetGpuExecutionStats();
  buffer.resize(10);   // shrink logical size, capacity untouched
  buffer.resize(100);  // grow back to the same size: no new allocation

  EXPECT_EQ(cfd::gpu::gpuExecutionStats().allocations, 0u);
  EXPECT_EQ(buffer.capacity(), capacityAfterGrow);
}

TEST(DeviceBufferTest, ResizePastCapacityAllocatesExactlyOnce) {
  skipIfNoDevice();
  cfd::gpu::resetGpuExecutionStats();
  DeviceBuffer<Real> buffer;
  buffer.resize(10);
  buffer.resize(1000);  // genuinely grows -- one real reallocation
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().allocations, 2u);
}

// P6-GPU-001: `allocations` (every cudaMalloc) and `reallocations` (the
// subset that grew an *already once-allocated* buffer) are distinct
// counters -- a buffer's first-ever allocation must never count as a
// reallocation, but every later capacity-growing resize() must.
TEST(DeviceBufferTest, FirstAllocationIsNotCountedAsReallocation) {
  skipIfNoDevice();
  cfd::gpu::resetGpuExecutionStats();
  DeviceBuffer<Real> buffer;
  buffer.resize(10);  // the very first allocation for this buffer
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().allocations, 1u);
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().reallocations, 0u);
}

TEST(DeviceBufferTest, GrowingAnAlreadyAllocatedBufferCountsAsReallocation) {
  skipIfNoDevice();
  DeviceBuffer<Real> buffer;
  buffer.resize(10);
  cfd::gpu::resetGpuExecutionStats();
  buffer.resize(1000);  // grows past capacity -- a genuine reallocation
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().allocations, 1u);
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().reallocations, 1u);
}

// The whole point of persistent residency: repeated resize()s that never
// grow past the high-water-mark capacity must count as neither an
// allocation nor a reallocation.
TEST(DeviceBufferTest, ResizeWithinCapacityCountsAsNeitherAllocationNorReallocation) {
  skipIfNoDevice();
  DeviceBuffer<Real> buffer;
  buffer.resize(100);
  cfd::gpu::resetGpuExecutionStats();
  for (int i = 0; i < 5; ++i) {
    buffer.resize(10);
    buffer.resize(100);
  }
  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
}

// P6-GPU-001: upload/download timing instrumentation -- a real
// cudaMemcpy must record a non-negative, and in practice strictly
// positive, wall-clock duration (host-side timer around a synchronous
// copy -- see DeviceBuffer::uploadFrom/downloadTo's own comments).
TEST(DeviceBufferTest, UploadAndDownloadRecordNonZeroTiming) {
  skipIfNoDevice();
  cfd::gpu::resetGpuExecutionStats();
  const std::vector<Real> host(10000, 3.25);
  DeviceBuffer<Real> buffer;
  buffer.uploadFrom(host.data(), host.size());
  EXPECT_GT(cfd::gpu::gpuExecutionStats().uploadSeconds, 0.0);
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().downloadSeconds, 0.0);

  std::vector<Real> roundTrip(host.size());
  buffer.downloadTo(roundTrip.data(), roundTrip.size());
  EXPECT_GT(cfd::gpu::gpuExecutionStats().downloadSeconds, 0.0);
}

// P6-GPU-001: resource cleanup -- a DeviceBuffer's destructor must
// actually free its device allocation (RAII), not merely reset its own
// bookkeeping.
TEST(DeviceBufferTest, DestructorFreesDeviceAllocation) {
  skipIfNoDevice();
  cfd::gpu::resetGpuExecutionStats();
  {
    DeviceBuffer<Real> buffer;
    buffer.resize(64);
  }
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().frees, 1u);
}

TEST(DeviceBufferTest, DownloadMoreThanLogicalSizeThrows) {
  skipIfNoDevice();
  DeviceBuffer<Real> buffer;
  buffer.resize(4);
  std::vector<Real> host(10);
  EXPECT_THROW(buffer.downloadTo(host.data(), 10), InvalidArgumentError);
}

TEST(DeviceBufferTest, MoveConstructorTransfersOwnership) {
  skipIfNoDevice();
  DeviceBuffer<Real> original;
  const std::vector<Real> host = {7.0, 8.0, 9.0};
  original.uploadFrom(host.data(), host.size());
  Real* originalPointer = original.data();

  DeviceBuffer<Real> moved(std::move(original));
  EXPECT_EQ(moved.data(), originalPointer);
  EXPECT_EQ(moved.size(), host.size());
  EXPECT_EQ(original.data(), nullptr);  // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(original.size(), 0u);       // NOLINT(bugprone-use-after-move)

  std::vector<Real> roundTrip(host.size());
  moved.downloadTo(roundTrip.data(), roundTrip.size());
  EXPECT_EQ(roundTrip, host);
}

TEST(DeviceBufferTest, MoveAssignmentReleasesPreviousAllocation) {
  skipIfNoDevice();
  DeviceBuffer<Real> a;
  a.resize(4);
  DeviceBuffer<Real> b;
  b.resize(8);

  cfd::gpu::resetGpuExecutionStats();
  a = std::move(b);
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().frees, 1u);  // a's original allocation released
  EXPECT_EQ(a.size(), 8u);
}

// ---------------------------------------------------------------------
// DeviceVector
// ---------------------------------------------------------------------

TEST(DeviceVectorTest, UploadDownloadRoundTrip) {
  skipIfNoDevice();
  const Vector host = makeVector(50, 0.73);
  DeviceVector device;
  device.uploadFrom(host);
  ASSERT_EQ(device.size(), host.size());

  const Vector roundTrip = device.downloadToVector();
  ASSERT_EQ(roundTrip.size(), host.size());
  for (Index i = 0; i < host.size(); ++i) {
    EXPECT_EQ(roundTrip[i], host[i]);
  }
}

// ---------------------------------------------------------------------
// DeviceCsrMatrix + persistent spmv()
// ---------------------------------------------------------------------

TEST(DeviceCsrMatrixTest, SpmvMatchesCpuReference) {
  skipIfNoDevice();
  const SparseMatrix matrix = makeGridMatrix(30, 30);
  const Vector x = makeVector(matrix.columns(), 0.41);
  const Vector cpu = matrix.multiply(x);

  DeviceCsrMatrix deviceMatrix;
  deviceMatrix.uploadStructureAndValues(matrix);
  DeviceVector deviceX;
  deviceX.uploadFrom(x);
  DeviceVector deviceY(matrix.rows());
  cfd::gpu::spmv(deviceMatrix, deviceX, deviceY);

  const Vector gpu = deviceY.downloadToVector();
  ASSERT_EQ(gpu.size(), cpu.size());
  for (Index i = 0; i < cpu.size(); ++i) {
    EXPECT_NEAR(gpu[i], cpu[i], 1e-9) << "row " << i;
  }
}

TEST(DeviceCsrMatrixTest, StructureUploadedExactlyOnceAcrossRepeatedValueUpdates) {
  skipIfNoDevice();
  const Index n = 20;
  SparseMatrix matrix = makeGridMatrix(n, n);
  const Vector x = makeVector(matrix.columns(), 1.0);

  DeviceCsrMatrix deviceMatrix;
  cfd::gpu::resetGpuExecutionStats();
  deviceMatrix.uploadStructureAndValues(matrix);
  // rowOffsets + columnIndices + values == 3 host-to-device transfers for
  // the very first (structure + values) upload.
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().hostToDeviceCalls, 3u);
  EXPECT_TRUE(deviceMatrix.hasStructure());

  DeviceVector deviceX;
  deviceX.uploadFrom(x);
  DeviceVector deviceY(matrix.rows());

  // Reset only after deviceX/deviceY's own first-time allocations above,
  // so the loop below measures purely the per-iteration cost.
  cfd::gpu::resetGpuExecutionStats();

  // This is the direct proof P6-PERF-001 requires: iteration 2..N must
  // NOT re-upload the CSR row/column structure, only the values.
  constexpr int kIterations = 5;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    matrix = makeGridMatrix(n, n, 0.1 + static_cast<Real>(iteration));  // same sparsity, new values
    deviceMatrix.updateValues(matrix);
    cfd::gpu::spmv(deviceMatrix, deviceX, deviceY);

    const Vector expected = matrix.multiply(x);
    const Vector actual = deviceY.downloadToVector();
    for (Index row = 0; row < expected.size(); ++row) {
      EXPECT_NEAR(actual[row], expected[row], 1e-9) << "iteration " << iteration << " row " << row;
    }
  }

  const cfd::gpu::GPUExecutionStats& stats = cfd::gpu::gpuExecutionStats();
  // Exactly one value-only upload per iteration -- no allocation ever
  // happens (capacity already suffices) and no additional
  // structure-sized transfer occurs.
  EXPECT_EQ(stats.hostToDeviceCalls, static_cast<std::uint64_t>(kIterations));
  EXPECT_EQ(stats.allocations, 0u);
}

TEST(DeviceCsrMatrixTest, UpdateValuesBeforeStructureUploadThrows) {
  skipIfNoDevice();
  const SparseMatrix matrix = makeGridMatrix(5, 5);
  DeviceCsrMatrix deviceMatrix;
  EXPECT_THROW(deviceMatrix.updateValues(matrix), InvalidArgumentError);
}

TEST(DeviceCsrMatrixTest, UpdateValuesWithChangedStructureThrows) {
  skipIfNoDevice();
  DeviceCsrMatrix deviceMatrix;
  deviceMatrix.uploadStructureAndValues(makeGridMatrix(5, 5));
  EXPECT_THROW(deviceMatrix.updateValues(makeGridMatrix(6, 6)), InvalidArgumentError);
}

// P6-GPU-001: the positive counterpart of the throw-on-mismatch test
// above -- a caller that detects a genuine structure change (e.g. a
// re-meshed case) and explicitly re-calls uploadStructureAndValues()
// gets a correctly-updated, working device matrix, not just a throw
// path. Also proves the *reallocation* accounting: growing from a 5x5
// to a 6x6 grid matrix genuinely grows all three device buffers.
TEST(DeviceCsrMatrixTest, ReUploadingStructureAfterAGenuineChangeWorksAndReallocates) {
  skipIfNoDevice();
  DeviceCsrMatrix deviceMatrix;
  deviceMatrix.uploadStructureAndValues(makeGridMatrix(5, 5));

  cfd::gpu::resetGpuExecutionStats();
  const SparseMatrix larger = makeGridMatrix(6, 6);
  deviceMatrix.uploadStructureAndValues(larger);

  EXPECT_EQ(deviceMatrix.rows(), larger.rows());
  EXPECT_EQ(deviceMatrix.columns(), larger.columns());
  EXPECT_EQ(deviceMatrix.nonZeros(), larger.nonZeros());
  // rowOffsets/columnIndices/values all grew past their 5x5 capacity --
  // three genuine reallocations, none of them this matrix's very first
  // allocation.
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().reallocations, 3u);

  const Vector x = makeVector(larger.columns(), 0.63);
  DeviceVector deviceX;
  deviceX.uploadFrom(x);
  DeviceVector deviceY(larger.rows());
  cfd::gpu::spmv(deviceMatrix, deviceX, deviceY);

  const Vector expected = larger.multiply(x);
  const Vector actual = deviceY.downloadToVector();
  for (Index row = 0; row < expected.size(); ++row) {
    EXPECT_NEAR(actual[row], expected[row], 1e-9) << "row " << row;
  }
}

TEST(DeviceCsrMatrixTest, SpmvMismatchedInputSizeThrows) {
  skipIfNoDevice();
  DeviceCsrMatrix deviceMatrix;
  deviceMatrix.uploadStructureAndValues(makeGridMatrix(5, 5));
  DeviceVector wrongSizeX(deviceMatrix.columns() + 1);
  DeviceVector y(deviceMatrix.rows());
  EXPECT_THROW(cfd::gpu::spmv(deviceMatrix, wrongSizeX, y), InvalidArgumentError);
}

TEST(DeviceCsrMatrixTest, SpmvMismatchedOutputSizeThrows) {
  skipIfNoDevice();
  DeviceCsrMatrix deviceMatrix;
  deviceMatrix.uploadStructureAndValues(makeGridMatrix(5, 5));
  DeviceVector x(deviceMatrix.columns());
  DeviceVector wrongSizeY(deviceMatrix.rows() + 1);
  EXPECT_THROW(cfd::gpu::spmv(deviceMatrix, x, wrongSizeY), InvalidArgumentError);
}

// ---------------------------------------------------------------------
// DeviceField / SyncState
// ---------------------------------------------------------------------

TEST(DeviceFieldTest, StartsHostDirty) {
  skipIfNoDevice();
  DeviceField field;
  EXPECT_EQ(field.state(), SyncState::HostDirty);
}

TEST(DeviceFieldTest, SyncToDeviceThenToHostRoundTrips) {
  skipIfNoDevice();
  Field<Real> host(25, 0.0);
  for (Index i = 0; i < host.size(); ++i) host[i] = static_cast<Real>(i) * 1.5;

  DeviceField field;
  field.syncToDevice(host);
  EXPECT_EQ(field.state(), SyncState::Synchronized);

  Field<Real> roundTrip(host.size());
  field.syncToHost(roundTrip);
  EXPECT_EQ(field.state(), SyncState::Synchronized);
  for (Index i = 0; i < host.size(); ++i) {
    EXPECT_EQ(roundTrip[i], host[i]);
  }
}

TEST(DeviceFieldTest, MarkHostDirtyThenMarkDeviceDirtyTransitionsState) {
  skipIfNoDevice();
  Field<Real> host(10, 2.0);
  DeviceField field;
  field.syncToDevice(host);
  ASSERT_EQ(field.state(), SyncState::Synchronized);

  field.markHostDirty();
  EXPECT_EQ(field.state(), SyncState::HostDirty);

  field.syncToDevice(host);
  field.markDeviceDirty();
  EXPECT_EQ(field.state(), SyncState::DeviceDirty);
}

TEST(DeviceFieldTest, SyncToHostSizeMismatchThrows) {
  skipIfNoDevice();
  Field<Real> host(10, 1.0);
  DeviceField field;
  field.syncToDevice(host);

  Field<Real> wrongSize(5);
  EXPECT_THROW(field.syncToHost(wrongSize), InvalidArgumentError);
}

// P6-GPU-001: "download only when host data is actually required" --
// pushing a field to the device (or mutating it there and marking it
// dirty) must never, by itself, trigger a device-to-host transfer.
TEST(DeviceFieldTest, NoDeviceToHostCopyUntilSyncToHostIsExplicitlyCalled) {
  skipIfNoDevice();
  Field<Real> host(20, 4.0);
  DeviceField field;

  cfd::gpu::resetGpuExecutionStats();
  field.syncToDevice(host);
  field.markDeviceDirty();
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().deviceToHostCalls, 0u);

  Field<Real> roundTrip(host.size());
  field.syncToHost(roundTrip);
  EXPECT_EQ(cfd::gpu::gpuExecutionStats().deviceToHostCalls, 1u);
}

// ---------------------------------------------------------------------
// spmv() kernel timing
// ---------------------------------------------------------------------

TEST(SpmvKernelTimingTest, RecordsNonZeroKernelTime) {
  skipIfNoDevice();
  const SparseMatrix matrix = makeGridMatrix(40, 40);
  const Vector x = makeVector(matrix.columns(), 0.29);

  DeviceCsrMatrix deviceMatrix;
  deviceMatrix.uploadStructureAndValues(matrix);
  DeviceVector deviceX;
  deviceX.uploadFrom(x);
  DeviceVector deviceY(matrix.rows());

  cfd::gpu::resetGpuExecutionStats();
  cfd::gpu::spmv(deviceMatrix, deviceX, deviceY);

  const auto& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.kernelLaunches, 1u);
  EXPECT_EQ(stats.synchronizations, 1u);
  EXPECT_GT(stats.kernelSeconds, 0.0);
}
