#pragma once

// P6-PERF-001 -- Performance: CUDA-only. See DeviceBuffer.hpp's own
// header comment for the "never include from a CPU-only-compiled file"
// rule this file inherits.
#include "cfd/core/Exception.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"

namespace cfd::gpu {

// Which side currently holds the authoritative value. A field that has
// never been synced either way starts HostDirty (the host-constructed
// value is authoritative until explicitly pushed to the device).
enum class SyncState {
  HostDirty,
  DeviceDirty,
  Synchronized,
};

// A device-resident counterpart to cfd::fields::Field<cfd::Real> (e.g.
// u, v, p) with explicit host/device authority tracking, so neither side
// can be read while stale without the caller noticing. Not yet wired
// into ProjectRunner/SIMPLE: no GPU kernel operates on whole fields
// until P6-PERF-002's GPU Krylov solvers exist to consume them -- this
// is the tested primitive they will build on, not a production data path
// yet (wiring it into SIMPLE now, ahead of any consumer, would be
// exactly the unnecessary-abstraction-ahead-of-need this codebase's own
// conventions repeatedly warn against).
class DeviceField {
 public:
  DeviceField() = default;

  // Uploads `host`'s values to the device and marks Synchronized.
  void syncToDevice(const cfd::fields::Field<cfd::Real>& host) {
    buffer_.uploadFrom(host.data(), host.size());
    state_ = SyncState::Synchronized;
  }

  // Downloads the device buffer into `host` and marks Synchronized.
  // `host` must already be sized to match -- this never resizes a Field
  // itself (Field's own "caller decides its size" contract). Throws
  // InvalidArgumentError on a size mismatch.
  void syncToHost(cfd::fields::Field<cfd::Real>& host) {
    if (host.size() != buffer_.size()) {
      throw cfd::InvalidArgumentError("DeviceField::syncToHost: host field size mismatch");
    }
    buffer_.downloadTo(host.data(), buffer_.size());
    state_ = SyncState::Synchronized;
  }

  // Declares the host side was mutated out-of-band (without going
  // through this class): the device copy is now stale, and a caller
  // relying on device data must syncToDevice() again first.
  void markHostDirty() noexcept { state_ = SyncState::HostDirty; }

  // Declares a device kernel mutated the device buffer out-of-band: the
  // host copy is now stale, and a caller relying on host data must
  // syncToHost() again first.
  void markDeviceDirty() noexcept { state_ = SyncState::DeviceDirty; }

  [[nodiscard]] SyncState state() const noexcept { return state_; }
  [[nodiscard]] cfd::Real* deviceData() noexcept { return buffer_.data(); }
  [[nodiscard]] const cfd::Real* deviceData() const noexcept { return buffer_.data(); }
  [[nodiscard]] cfd::Index size() const noexcept { return buffer_.size(); }

 private:
  DeviceBuffer<cfd::Real> buffer_;
  SyncState state_{SyncState::HostDirty};
};

}  // namespace cfd::gpu
