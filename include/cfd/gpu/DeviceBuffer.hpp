#pragma once

// P6-PERF-001 -- Performance: the foundational RAII device-residency
// primitive. Only ever included from CUDA-aware translation units (part
// of the cfdcuda target, or a CFDAPP_ENABLE_CUDA-gated test/benchmark
// target) -- it pulls in <cuda_runtime.h> via CudaCheck.hpp, so it must
// never be included from a file that also builds in a CPU-only
// configuration (see GPUBackend.hpp's own header comment for why this
// codebase gates GPU code at the target level rather than #ifdef).
#include <cuda_runtime.h>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

// Owns one device allocation of `count` elements of T. Move-only (a
// device pointer has no meaningful copy without an extra device-to-
// device transfer no caller here needs). `resize()` only reallocates
// when growing -- shrinking or re-uploading the same size reuses the
// existing allocation, which is the whole point of this class: the
// stateless cfd::gpu::csrSpmvCuda() this replaces internally did a fresh
// cudaMalloc/cudaFree on every single call (see CsrSpmvKernel.cu's own
// history/comments) with zero reuse across calls.
template <typename T>
class DeviceBuffer {
 public:
  DeviceBuffer() = default;
  explicit DeviceBuffer(cfd::Index count) { resize(count); }

  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;

  DeviceBuffer(DeviceBuffer&& other) noexcept
      : pointer_(other.pointer_), size_(other.size_), capacity_(other.capacity_) {
    other.pointer_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }

  DeviceBuffer& operator=(DeviceBuffer&& other) noexcept {
    if (this != &other) {
      release();
      pointer_ = other.pointer_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      other.pointer_ = nullptr;
      other.size_ = 0;
      other.capacity_ = 0;
    }
    return *this;
  }

  ~DeviceBuffer() { release(); }

  // Sets the logical size to `count`, growing the underlying allocation
  // only if `count` exceeds the current capacity. count == 0 is a valid,
  // safe, no-allocation state (never calls cudaMalloc(ptr, 0)).
  void resize(cfd::Index count) {
    if (count > capacity_) {
      // Captured before release() zeroes capacity_ -- true iff this
      // buffer already held a real allocation (i.e. this growth is a
      // *re*allocation, not the buffer's first-ever cudaMalloc).
      const bool hadPriorAllocation = capacity_ > 0;
      release();
      if (count > 0) {
        checkCuda(cudaMalloc(reinterpret_cast<void**>(&pointer_), count * sizeof(T)),
                  "cudaMalloc(DeviceBuffer)");
        auto& stats = gpuExecutionStats();
        ++stats.allocations;
        if (hadPriorAllocation) ++stats.reallocations;
      }
      capacity_ = count;
    }
    size_ = count;
  }

  // Resizes to `count` (reusing capacity when possible) and copies
  // `count` elements from `hostData` to the device. Records the transfer
  // (and its wall-clock duration) in GPUExecutionStats.
  void uploadFrom(const T* hostData, cfd::Index count) {
    resize(count);
    if (count == 0) return;
    cfd::Timer timer;
    checkCuda(cudaMemcpy(pointer_, hostData, count * sizeof(T), cudaMemcpyHostToDevice),
              "cudaMemcpy(DeviceBuffer upload)");
    auto& stats = gpuExecutionStats();
    stats.uploadSeconds += timer.elapsedSeconds();
    stats.hostToDeviceBytes += static_cast<std::uint64_t>(count) * sizeof(T);
    ++stats.hostToDeviceCalls;
  }

  // Copies the first `count` elements to `hostData`. Throws
  // InvalidArgumentError if count exceeds the current logical size --
  // never reads past what was actually written.
  void downloadTo(T* hostData, cfd::Index count) const {
    if (count > size_) {
      throw cfd::InvalidArgumentError("DeviceBuffer::downloadTo: count exceeds buffer size");
    }
    if (count == 0) return;
    cfd::Timer timer;
    checkCuda(cudaMemcpy(hostData, pointer_, count * sizeof(T), cudaMemcpyDeviceToHost),
              "cudaMemcpy(DeviceBuffer download)");
    auto& stats = gpuExecutionStats();
    stats.downloadSeconds += timer.elapsedSeconds();
    stats.deviceToHostBytes += static_cast<std::uint64_t>(count) * sizeof(T);
    ++stats.deviceToHostCalls;
  }

  [[nodiscard]] T* data() noexcept { return pointer_; }
  [[nodiscard]] const T* data() const noexcept { return pointer_; }
  [[nodiscard]] cfd::Index size() const noexcept { return size_; }
  [[nodiscard]] cfd::Index capacity() const noexcept { return capacity_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

 private:
  void release() noexcept {
    if (pointer_ != nullptr) {
      cudaFree(pointer_);
      ++gpuExecutionStats().frees;
      pointer_ = nullptr;
    }
    capacity_ = 0;
    size_ = 0;
  }

  T* pointer_{nullptr};
  cfd::Index size_{0};
  cfd::Index capacity_{0};
};

}  // namespace cfd::gpu
