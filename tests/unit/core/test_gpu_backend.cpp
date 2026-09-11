// P6-PERF-001 -- Performance: unlike tests/unit/gpu/test_cuda_spmv.cpp
// (only compiled when CFDAPP_ENABLE_CUDA=ON), this file is part of
// CFDCoreTests and therefore always built and run, in both build
// configurations -- proving the CPU-only fallback path stays honest:
// cfd::gpu::cudaAvailable() must never throw, and cfd::gpu::
// GPUExecutionStats (plain counters, no CUDA dependency, always compiled
// into cfdcore -- see GPUExecutionStats.hpp's own header comment) must
// start at all-zero in a binary that never touches a device buffer.
#include <gtest/gtest.h>

#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

TEST(GpuBackendCpuFallbackTest, CudaAvailableNeverThrows) {
  EXPECT_NO_THROW((void)cfd::gpu::cudaAvailable());
}

TEST(GpuBackendCpuFallbackTest, ExecutionStatsStartAtZeroWithNoDeviceActivity) {
  const cfd::gpu::GPUExecutionStats& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.hostToDeviceBytes, 0u);
  EXPECT_EQ(stats.deviceToHostBytes, 0u);
  EXPECT_EQ(stats.hostToDeviceCalls, 0u);
  EXPECT_EQ(stats.deviceToHostCalls, 0u);
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.reallocations, 0u);
  EXPECT_EQ(stats.frees, 0u);
  EXPECT_EQ(stats.kernelLaunches, 0u);
  EXPECT_EQ(stats.synchronizations, 0u);
  EXPECT_EQ(stats.uploadSeconds, 0.0);
  EXPECT_EQ(stats.downloadSeconds, 0.0);
  EXPECT_EQ(stats.kernelSeconds, 0.0);
  EXPECT_EQ(stats.dotSeconds, 0.0);
  EXPECT_EQ(stats.vectorOpSeconds, 0.0);
  EXPECT_EQ(stats.gpuSolveSeconds, 0.0);
  EXPECT_EQ(stats.gpuLinearSolves, 0u);
  EXPECT_EQ(stats.gpuLinearSolverIterations, 0u);
  EXPECT_EQ(stats.gpuBackendFallbacks, 0u);
}

TEST(GpuBackendCpuFallbackTest, ResetIsANoOpWhenAlreadyZero) {
  cfd::gpu::resetGpuExecutionStats();
  const cfd::gpu::GPUExecutionStats& stats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(stats.allocations, 0u);
  EXPECT_EQ(stats.hostToDeviceCalls, 0u);
}
