// GPU-PIPE-001 Persistent Fields -- the two device-side operations that let the
// production SIMPLE state stay resident across outer iterations.
//
// Both are trivially small. They exist so that `pressure`, `velocity` and
// `massFlux` never have to make a host round trip whose only purpose is to hand
// the device back bytes the device itself produced.
//
// ---------------------------------------------------------------------------
// WHY THIS IS ITS OWN TRANSLATION UNIT, AND WHY IT IS -fmad=false
// ---------------------------------------------------------------------------
// The pressure update is
//
//     p_new[c] = p[c] + (alpha * pPrime[c])
//
// transcribed from SIMPLE.cpp's host loop
//
//     pressureNew[id] = pressure[id] + (relaxation.pressure * pPrime[id]);
//
// That is exactly the `c + a*b` form nvcc contracts into a single FMA by
// default -- more accurate, and therefore a DIFFERENT number from the CPU's
// rounded-multiply-then-rounded-add. GPU-DISC-001 established BITWISE CPU/GPU
// equivalence and `full_solve_equivalence` compares whole residual histories
// bitwise, so a contracted update would break an already-qualified gate.
//
// DeviceVectorOpsKernel.cu has an axpy that would otherwise fit, but it is NOT
// in cuda/CMakeLists.txt's -fmad=false list -- reusing it would silently change
// the answer. Hence a separate TU, added to that list.
//
// The carry kernels do no arithmetic at all; they are here because they belong
// to the same concern and cost nothing.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DevicePersistentFields.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index n) {
  return static_cast<int>((n + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

// SIMPLE.cpp's relaxed pressure update, per cell, in the same association
// order. No FMA (see this file's header).
__global__ void relaxedPressureUpdateKernel(Index cellCount, const Real* __restrict__ pressure,
                                            const Real* __restrict__ pPrime, Real alpha,
                                            Real* __restrict__ out) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  out[c] = pressure[c] + (alpha * pPrime[c]);
}

// Counts entries that are not finite. A PREDICATE, not a value that feeds the
// solution, so the reduction order is irrelevant and no bitwise question
// arises -- unlike every other reduction in this codebase.
//
// It exists so SIMPLE's per-iteration NonFiniteState guard survives field
// residency: without it the guard would need velocity and pressure downloaded
// every iteration, which is exactly what this gate removes. Eight bytes cross
// the boundary instead of five full fields.
__global__ void countNonFiniteKernel(Index count, const Real* __restrict__ values,
                                     unsigned int* __restrict__ out) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  if (!isfinite(values[i])) atomicAdd(out, 1u);
}

// A constant fill. No arithmetic, so this translation unit's -fmad=false is
// irrelevant to it; it lives here because it belongs to the same concern.
__global__ void fillFieldKernel(Index count, Real value, Real* __restrict__ out) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  out[i] = value;
}

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void relaxedPressureUpdateDevice(Index cellCount, const DeviceBuffer<Real>& pressure,
                                 const DeviceBuffer<Real>& pressureCorrection, Real alpha,
                                 DeviceBuffer<Real>& out) {
  if (pressure.size() < cellCount || pressureCorrection.size() < cellCount) {
    throw cfd::InvalidArgumentError(
        "relaxedPressureUpdateDevice: pressure or pressure-correction buffer is not resident");
  }
  out.resize(cellCount);
  if (cellCount == 0) return;
  cfd::Timer timer;
  relaxedPressureUpdateKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, pressure.data(), pressureCorrection.data(), alpha, out.data());
  recordLaunch("relaxedPressureUpdateKernel launch", timer);
}

// The counter is supplied by the caller and RESET here with a device-side
// memset, not an upload.
//
// A first version allocated a counter locally, uploaded a zero into it and
// downloaded the result -- per field, per iteration. Measured cost: 4
// allocations, 4 H2D and 4 D2H every outer iteration, which ate most of the
// residency win it was meant to protect and broke the "zero steady-state
// allocations" criterion outright. Accumulating every field into ONE
// caller-owned counter costs 0 allocations, 0 H2D and a single 4-byte D2H.
void resetNonFiniteCounter(DeviceBuffer<unsigned int>& counter) {
  counter.resize(1);
  checkCuda(cudaMemset(counter.data(), 0, sizeof(unsigned int)),
            "cudaMemset(nonFinite counter)");
}

void countNonFiniteDevice(Index count, const DeviceBuffer<Real>& values,
                          DeviceBuffer<unsigned int>& counter) {
  if (count == 0) return;
  if (values.size() < count) {
    throw cfd::InvalidArgumentError("countNonFiniteDevice: buffer is not resident");
  }
  // NOTE: this is the FIRST atomic in this codebase. GPU-DISC-001O's audit
  // recorded "atomics NONE anywhere", and that is no longer true -- stated
  // rather than left for someone to rediscover. It is a single-counter
  // atomicAdd, race-free by construction, and it gives racecheck an atomic to
  // examine where it previously had none, which strengthens that tool's
  // coverage rather than weakening it.
  cfd::Timer timer;
  countNonFiniteKernel<<<blockCountFor(count), kThreadsPerBlock>>>(count, values.data(),
                                                                   counter.data());
  recordLaunch("countNonFiniteKernel launch", timer);
}

bool readNonFiniteCounter(const DeviceBuffer<unsigned int>& counter) {
  unsigned int nonFinite = 0;
  counter.downloadTo(&nonFinite, 1);
  return nonFinite == 0;
}

void fillFieldDevice(Index count, Real value, DeviceBuffer<Real>& out) {
  out.resize(count);
  if (count == 0) return;
  cfd::Timer timer;
  fillFieldKernel<<<blockCountFor(count), kThreadsPerBlock>>>(count, value, out.data());
  recordLaunch("fillFieldKernel launch", timer);
}

void carryFieldDevice(Index count, const DeviceBuffer<Real>& from, DeviceBuffer<Real>& to) {
  if (from.size() < count) {
    throw cfd::InvalidArgumentError("carryFieldDevice: source buffer is not resident");
  }
  to.resize(count);
  if (count == 0) return;
  // A device-to-device copy, NOT a host round trip. It is counted as neither an
  // H2D nor a D2H in GPUExecutionStats, which is correct: nothing crosses PCIe.
  checkCuda(cudaMemcpy(to.data(), from.data(), static_cast<std::size_t>(count) * sizeof(Real),
                       cudaMemcpyDeviceToDevice),
            "cudaMemcpy(carryFieldDevice)");
}

}  // namespace cfd::gpu
