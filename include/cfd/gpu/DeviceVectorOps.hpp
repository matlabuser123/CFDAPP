#pragma once

// P6-GPU-002 -- Performance: CUDA-only. See DeviceBuffer.hpp's own
// header comment for the "never include from a CPU-only-compiled file"
// rule this file inherits. The small, general vector-op kernel set
// GpuCG/GpuBiCGSTAB (cuda/kernels/GpuLinearSolverCuda.cpp) are built
// from -- deliberately mirrors cfd::algebra::Vector's own operator+/-/*,
// dot(), and l2Norm() (one GPU analogue of each CPU primitive an
// iterative Krylov solver needs, not a new algorithm), the same
// "correctness judged by direct comparison against the CPU version"
// precedent CudaSpmv.hpp's own header comment sets.
#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceVector.hpp"

namespace cfd::gpu {

// w = a*x + b*y (element-wise). Covers every linear combination a Krylov
// iteration needs: axpy(alpha, x, y) is waxpby(alpha, x, 1, y, y); a
// plain scale is waxpby(alpha, x, 0, x, x); a plain copy is
// waxpby(1, x, 0, x, w). `w` may alias `x` and/or `y`. Throws
// InvalidArgumentError on a size mismatch.
void waxpby(cfd::Real a, const DeviceVector& x, cfd::Real b, const DeviceVector& y,
            DeviceVector& w);

// y += alpha * x (in place). Throws InvalidArgumentError on a size
// mismatch.
void axpy(cfd::Real alpha, const DeviceVector& x, DeviceVector& y);

// dst = src (device-to-device, no host round trip). Resizes dst to
// src's size first (reusing capacity per DeviceVector::resize's own
// contract).
void deviceCopy(DeviceVector& dst, const DeviceVector& src);

// v[:] = value. Resizes v to `count` first if v's current size differs.
void fill(DeviceVector& v, cfd::Index count, cfd::Real value);

// dot(a, b) = sum(a[i] * b[i]) -- a block-level reduction kernel followed
// by a *small* host-side sum over one partial-sum-per-block device
// buffer (its size is the CUDA grid's block count, not `a`/`b`'s own
// size -- e.g. a few hundred Reals for a million-row system, not a
// whole-vector round trip). This is the "small scalar synchronization...
// acceptable where necessary, but instrument it" this task's own scope
// explicitly allows for convergence-check reductions; the download is
// counted in GPUExecutionStats like any other DeviceBuffer transfer, and
// the kernel/reduction time itself in dotSeconds. Throws
// InvalidArgumentError on a size mismatch.
[[nodiscard]] cfd::Real dot(const DeviceVector& a, const DeviceVector& b);

// GPU-PIPE-001 Phase 2B: dot(a0,b0) and dot(a1,b1) in ONE kernel launch, one
// synchronization and one 16-byte download, instead of two of each.
//
// **Bitwise identical** to calling dot() twice -- not "within tolerance".
// Each product keeps its own block partition, its own shared-memory halving
// tree and its own partial-sum slice, and the two slices are finalised by the
// same in-order serial sum dot() uses, so every floating-point operation and
// its order is unchanged. Fusion changes only when the work is scheduled.
//
// Exists because Phase 1 measured the reduction round trip at 79-101 us
// regardless of payload: the cost is per *call*, so halving the number of calls
// is what helps. All four vectors must share a length; throws
// InvalidArgumentError otherwise.
void dot2(const DeviceVector& a0, const DeviceVector& b0, const DeviceVector& a1,
          const DeviceVector& b1, cfd::Real& out0, cfd::Real& out1);

// sqrt(dot(v, v)).
[[nodiscard]] cfd::Real l2Norm(const DeviceVector& v);

// absDot(a, b) = sum(|a[i] * b[i]|) -- dot()'s reduction over the magnitudes
// of the terms rather than the terms themselves, so it cannot cancel. It
// bounds the rounding error of dot(a, b)'s own sum, which is what decides
// whether a computed inner product can be distinguished from zero:
// GpuBiCGSTAB's scale-relative breakdown test needs exactly this quantity,
// mirroring cfd::algebra::BiCGSTAB's cancelledToRoundingLevel (GPU-PCORR-001).
// Same cost and instrumentation as dot(). Throws InvalidArgumentError on a
// size mismatch.
[[nodiscard]] cfd::Real absDot(const DeviceVector& a, const DeviceVector& b);

}  // namespace cfd::gpu
