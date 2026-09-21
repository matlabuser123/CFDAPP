#pragma once

// GPU-PIPE-001 Persistent Fields -- device-side operations that keep production
// SIMPLE state resident across outer iterations.
//
// Both entry points exist to remove a host round trip whose only purpose was to
// hand the device back bytes the device itself produced. See
// cuda/kernels/DevicePersistentFieldsKernel.cu for why the pressure update
// needs its own -fmad=false translation unit.

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"

namespace cfd::gpu {

// SIMPLE's relaxed pressure update, on the device:
//
//     out[c] = pressure[c] + (alpha * pressureCorrection[c])
//
// BITWISE identical to SIMPLE.cpp's host loop -- same association order, and
// compiled without FMA contraction. `out` may alias neither input.
void relaxedPressureUpdateDevice(cfd::Index cellCount, const DeviceBuffer<cfd::Real>& pressure,
                                 const DeviceBuffer<cfd::Real>& pressureCorrection, cfd::Real alpha,
                                 DeviceBuffer<cfd::Real>& out);

// A device-to-device field copy. Nothing crosses PCIe, so it is deliberately
// NOT counted as a transfer in GPUExecutionStats -- a carry that showed up as
// an H2D would make the residency measurement lie.
void carryFieldDevice(cfd::Index count, const DeviceBuffer<cfd::Real>& from,
                      DeviceBuffer<cfd::Real>& to);

// Writes `value` into `count` entries of `out`, on the device.
//
// GPU-PIPE-001 final residency: a 2D solve must hold exactly zero in the W
// component of the momentum predictor, because SIMPLE's combineComponents
// leaves it zero and the device must not carry a stale value there. That was
// done by uploading a host std::vector of zeros EVERY iteration -- a
// full-field H2D of bytes the device can write itself. The stored result is
// identical (+0.0 either way, no arithmetic involved), so this removes a
// transfer without touching a number.
void fillFieldDevice(cfd::Index count, cfd::Real value, DeviceBuffer<cfd::Real>& out);

// SIMPLE's per-iteration NonFiniteState guard, on the device, so velocity and
// pressure need not be downloaded every iteration: FOUR BYTES cross the
// boundary instead of five full fields.
//
// A PREDICATE, so reduction order carries no bitwise consequence -- the one
// reduction in this codebase for which that is true.
//
// Three calls rather than one because the counter must be caller-owned: a
// locally allocated counter costs an allocation, an upload and a download PER
// FIELD PER ITERATION, which measurably broke both the transfer and the
// zero-steady-state-allocation criteria this gate exists to satisfy. Reset
// once, accumulate every field into it, read once.
void resetNonFiniteCounter(DeviceBuffer<unsigned int>& counter);
void countNonFiniteDevice(cfd::Index count, const DeviceBuffer<cfd::Real>& values,
                          DeviceBuffer<unsigned int>& counter);
[[nodiscard]] bool readNonFiniteCounter(const DeviceBuffer<unsigned int>& counter);

}  // namespace cfd::gpu
