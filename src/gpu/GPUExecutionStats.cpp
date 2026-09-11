#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

GPUExecutionStats& gpuExecutionStats() noexcept {
  static GPUExecutionStats stats;
  return stats;
}

void resetGpuExecutionStats() noexcept { gpuExecutionStats() = GPUExecutionStats{}; }

}  // namespace cfd::gpu
