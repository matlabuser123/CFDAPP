// P6-GPU-001 -- Performance: unlike tests/unit/gpu/
// test_gpu_residency_manager.cpp (only compiled when CFDAPP_ENABLE_CUDA=
// ON), this file is part of CFDCoreTests and therefore always built and
// run, in both build configurations -- cfd::gpu::GpuResidencyManager's
// header has no CUDA dependency (see its own header comment), so it can
// be exercised here the same way cfd::gpu::cudaAvailable() is in
// test_gpu_backend.cpp: proving the CPU-only/no-device fallback path
// never throws and never claims activity that did not happen.
#include <gtest/gtest.h>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuResidencyManager.hpp"

using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::fields::ScalarField;
using cfd::gpu::GpuResidencyManager;

namespace {

cfd::algebra::SparseMatrix makeTinyMatrix() {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 2.0);
  builder.add(1, 1, 2.0);
  builder.add(2, 2, 2.0);
  return builder.build();
}

}  // namespace

TEST(GpuResidencyManagerCpuFallbackTest, ActiveMatchesCudaAvailable) {
  // Documented contract (GpuResidencyManager.hpp's own header comment):
  // active() mirrors cfd::gpu::cudaAvailable() exactly -- true only in a
  // CUDA-enabled build with a usable device at construction time.
  const GpuResidencyManager manager;
  EXPECT_EQ(manager.active(), cfd::gpu::cudaAvailable());
}

TEST(GpuResidencyManagerCpuFallbackTest, SyncMatrixNeverThrowsRegardlessOfDevice) {
  GpuResidencyManager manager;
  const auto matrix = makeTinyMatrix();
  EXPECT_NO_THROW(manager.syncMatrix("test", matrix));
  // A second call with the identical structure must also never throw --
  // this is the value-only-update path when active(), and a pure no-op
  // otherwise.
  EXPECT_NO_THROW(manager.syncMatrix("test", matrix));
}

TEST(GpuResidencyManagerCpuFallbackTest, SyncFieldNeverThrowsRegardlessOfDevice) {
  GpuResidencyManager manager;
  const ScalarField field(5, 1.5);
  EXPECT_NO_THROW(manager.syncField("test", field));
  EXPECT_NO_THROW(manager.syncField("test", field));
}

TEST(GpuResidencyManagerCpuFallbackTest, MoveConstructionAndAssignmentAreSafe) {
  GpuResidencyManager original;
  GpuResidencyManager moved(std::move(original));  // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(moved.active(), cfd::gpu::cudaAvailable());

  GpuResidencyManager target;
  target = std::move(moved);  // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(target.active(), cfd::gpu::cudaAvailable());
}
