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

// sqrt(dot(v, v)).
[[nodiscard]] cfd::Real l2Norm(const DeviceVector& v);

}  // namespace cfd::gpu
