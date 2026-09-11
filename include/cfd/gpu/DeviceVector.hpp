#pragma once

// P6-PERF-001 -- Performance: CUDA-only. See DeviceBuffer.hpp's own
// header comment for the "never include from a CPU-only-compiled file"
// rule this file inherits.
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"

namespace cfd::gpu {

// A device-resident counterpart to cfd::algebra::Vector. Reuses its
// device allocation across repeated upload()s of the same size -- the
// persistent-residency building block Krylov solvers (P6-PERF-002) need
// for RHS/solution/work vectors that keep the same length across every
// iteration of a solve.
class DeviceVector {
 public:
  DeviceVector() = default;
  explicit DeviceVector(cfd::Index size) { buffer_.resize(size); }

  void uploadFrom(const cfd::algebra::Vector& host) {
    buffer_.uploadFrom(host.data(), host.size());
  }

  [[nodiscard]] cfd::algebra::Vector downloadToVector() const {
    cfd::algebra::Vector host(buffer_.size());
    buffer_.downloadTo(host.data(), buffer_.size());
    return host;
  }

  void resize(cfd::Index size) { buffer_.resize(size); }

  [[nodiscard]] cfd::Real* data() noexcept { return buffer_.data(); }
  [[nodiscard]] const cfd::Real* data() const noexcept { return buffer_.data(); }
  [[nodiscard]] cfd::Index size() const noexcept { return buffer_.size(); }
  [[nodiscard]] bool empty() const noexcept { return buffer_.empty(); }

 private:
  DeviceBuffer<cfd::Real> buffer_;
};

}  // namespace cfd::gpu
