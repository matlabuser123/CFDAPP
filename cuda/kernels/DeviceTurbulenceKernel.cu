// P16-GPU-TURB -- the first turbulence physics on the device.
//
// See DeviceTurbulence.hpp for what this reuses (the already-qualified
// GreenGauss velocity gradient) and what is new (the three cell-local
// algebraic steps). Compiled with -fmad=false like every other qualified
// kernel, because the target is bitwise agreement with the host and an FMA
// would produce a different, more accurate number.

#include <cuda_runtime.h>

#include <cmath>
#include <stdexcept>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceTurbulence.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

// Records the backend and reports whether the device path may run.
//
// The injector is honoured HERE rather than at a call site so that the
// negative control exercises the same code production does. A forced fallback
// launches no kernel and leaves the output untouched, which is what makes it
// detectable: a gate that only checked the numbers would pass on a stale
// buffer.
bool deviceAllowed(TurbulenceOperation operation) {
  if (cpuFallbackForcedForTesting(operation)) {
    turbulenceBackendTrace().record(operation, ExecutionBackend::Cpu);
    return false;
  }
  turbulenceBackendTrace().record(operation, ExecutionBackend::Gpu);
  return true;
}

void accountKernel(const cfd::Timer& timer) {
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

// S^2 = 2*(du/dx)^2 + 2*(dv/dy)^2 + (du/dy + dv/dx)^2
//
// Transcribed from cfd::turbulence::computeStrainRateMagnitudeSquared term for
// term, including the shear sum being formed FIRST and squared once. Writing
// it as `dudy*dudy + 2*dudy*dvdx + dvdx*dvdx` is the same algebra and a
// different double.
__global__ void strainRateSquaredKernel(Index cellCount, const Real* __restrict__ gradUx,
                                        const Real* __restrict__ gradUy,
                                        const Real* __restrict__ gradVx,
                                        const Real* __restrict__ gradVy,
                                        Real* __restrict__ strainRateSquared) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real dudx = gradUx[cell];
  const Real dudy = gradUy[cell];
  const Real dvdx = gradVx[cell];
  const Real dvdy = gradVy[cell];
  const Real shear = dudy + dvdx;
  strainRateSquared[cell] = (2.0 * dudx * dudx) + (2.0 * dvdy * dvdy) + (shear * shear);
}

// P_k = mu_t * S^2. mu_t is the LEFT operand, as on the host.
__global__ void productionKernel(Index cellCount, const Real* __restrict__ turbulentViscosity,
                                 const Real* __restrict__ strainRateSquared,
                                 Real* __restrict__ production) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  production[cell] = turbulentViscosity[cell] * strainRateSquared[cell];
}

// mu_t = density * cMu * kSafe * kSafe / epsSafe
//
// fmax(x, floor) is NOT used: std::max(a, b) on the host returns `a` unless
// `b > a`, and fmax differs from that on a NaN input. A NaN k would silently
// become the floor here and stay NaN on the host, so the two backends would
// disagree exactly where a solve had already gone wrong and the difference
// would be hardest to read.
__global__ void turbulentViscosityKernel(Index cellCount, Real density, Real cMu,
                                         const Real* __restrict__ k,
                                         const Real* __restrict__ epsilon, Real kFloor,
                                         Real epsilonFloor, Real* __restrict__ turbulentViscosity) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real kValue = k[cell];
  const Real epsilonValue = epsilon[cell];
  const Real kSafe = (kFloor > kValue) ? kFloor : kValue;
  const Real epsSafe = (epsilonFloor > epsilonValue) ? epsilonFloor : epsilonValue;
  turbulentViscosity[cell] = density * cMu * kSafe * kSafe / epsSafe;
}

// Gamma = mu + mu_t/sigma. `invalid` is set when an entry would make the host
// throw, so the device fails closed on the same condition.
__global__ void effectiveDiffusivityKernel(Index cellCount, Real molecularViscosity,
                                           const Real* __restrict__ turbulentViscosity, Real sigma,
                                           Real* __restrict__ diffusivity,
                                           int* __restrict__ invalid) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real muT = turbulentViscosity[cell];
  if (!isfinite(muT) || muT < 0.0) *invalid = 1;
  diffusivity[cell] = molecularViscosity + (muT / sigma);
}

// mu_eff = mu + mu_t, the device port of TurbulenceModel::effectiveViscosity.
//
// NOT effectiveDiffusivityKernel with sigma = 1. That kernel computes a
// turbulence CLOSURE, mu + mu_t/sigma_phi, and the two happen to coincide at
// sigma = 1 only as an arithmetic accident. The host keeps them as two separate
// functions for the same reason, and folding them here would mean a later
// change to the closure -- a floor, a different Prandtl-Schmidt convention --
// silently changed the momentum viscosity too.
__global__ void effectiveViscosityKernel(Index cellCount, Real molecularViscosity,
                                         const Real* __restrict__ turbulentViscosity,
                                         Real* __restrict__ effective,
                                         int* __restrict__ invalid) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real muT = turbulentViscosity[cell];
  if (!isfinite(muT) || muT < 0.0) *invalid = 1;
  effective[cell] = molecularViscosity + muT;
}

// Su = P_k, Sp = -rho*epsilon/max(k, kFloor).
__global__ void kSourceKernel(Index cellCount, Real density, const Real* __restrict__ k,
                              const Real* __restrict__ epsilon, const Real* __restrict__ production,
                              Real kFloor, Real* __restrict__ Su, Real* __restrict__ Sp) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real kValue = k[cell];
  const Real kSafe = (kFloor > kValue) ? kFloor : kValue;
  Su[cell] = production[cell];
  Sp[cell] = -density * epsilon[cell] / kSafe;
}

// field = max(field, floorValue), std::max semantics (keeps `field` on a tie).
__global__ void scalarFloorKernel(Index cellCount, Real floorValue, Real* __restrict__ field) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real value = field[cell];
  if (floorValue > value) field[cell] = floorValue;
}

// Su = c1*epsOverK*P_k, Sp = -c2*rho*epsOverK, with epsOverK formed once.
__global__ void epsilonSourceKernel(Index cellCount, Real density, Real c1Epsilon, Real c2Epsilon,
                                    const Real* __restrict__ k, const Real* __restrict__ epsilon,
                                    const Real* __restrict__ production, Real kFloor,
                                    Real* __restrict__ Su, Real* __restrict__ Sp) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real kValue = k[cell];
  const Real kSafe = (kFloor > kValue) ? kFloor : kValue;
  const Real epsOverK = epsilon[cell] / kSafe;
  Su[cell] = c1Epsilon * epsOverK * production[cell];
  Sp[cell] = -c2Epsilon * density * epsOverK;
}

// field[cell] = value, for each imposed cell. One thread per imposed cell.
__global__ void reassertImposedKernel(Index imposedCount, const Index* __restrict__ cells,
                                      const Real* __restrict__ values, Real* __restrict__ field) {
  const Index slot = blockIdx.x * blockDim.x + threadIdx.x;
  if (slot >= imposedCount) return;
  field[cells[slot]] = values[slot];
}

// out[slot] = source[indices[slot]].
__global__ void gatherByIndexKernel(Index count, const Index* __restrict__ indices,
                                    const Real* __restrict__ source, Real* __restrict__ out) {
  const Index slot = blockIdx.x * blockDim.x + threadIdx.x;
  if (slot >= count) return;
  out[slot] = source[indices[slot]];
}

}  // namespace

void computeStrainRateMagnitudeSquaredDevice(const DeviceVelocityGradient& gradient,
                                             Index cellCount,
                                             DeviceBuffer<Real>& strainRateSquared) {
  if (!deviceAllowed(TurbulenceOperation::StrainRate)) return;
  if (cellCount == 0) return;
  if (gradient.gradUx.size() < cellCount || gradient.gradUy.size() < cellCount ||
      gradient.gradVx.size() < cellCount || gradient.gradVy.size() < cellCount) {
    throw std::runtime_error(
        "computeStrainRateMagnitudeSquaredDevice: velocity gradient is smaller than the cell "
        "count");
  }
  strainRateSquared.resize(cellCount);

  cfd::Timer timer;
  strainRateSquaredKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, gradient.gradUx.data(), gradient.gradUy.data(), gradient.gradVx.data(),
      gradient.gradVy.data(), strainRateSquared.data());
  checkCuda(cudaGetLastError(), "strainRateSquaredKernel");
  checkCuda(cudaDeviceSynchronize(), "strainRateSquaredKernel sync");
  accountKernel(timer);
}

void computeTurbulentProductionDevice(const DeviceBuffer<Real>& turbulentViscosity,
                                      const DeviceBuffer<Real>& strainRateSquared,
                                      DeviceBuffer<Real>& production) {
  if (!deviceAllowed(TurbulenceOperation::Production)) return;
  const Index cellCount = turbulentViscosity.size();
  if (cellCount == 0) return;
  if (strainRateSquared.size() != cellCount) {
    throw std::runtime_error("computeTurbulentProductionDevice: mu_t and S^2 differ in length");
  }
  production.resize(cellCount);

  cfd::Timer timer;
  productionKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, turbulentViscosity.data(), strainRateSquared.data(), production.data());
  checkCuda(cudaGetLastError(), "productionKernel");
  checkCuda(cudaDeviceSynchronize(), "productionKernel sync");
  accountKernel(timer);
}

void computeTurbulentViscosityDevice(Real density, Real cMu, const DeviceBuffer<Real>& k,
                                     const DeviceBuffer<Real>& epsilon, Real kFloor,
                                     Real epsilonFloor, DeviceBuffer<Real>& turbulentViscosity) {
  if (!deviceAllowed(TurbulenceOperation::TurbulentViscosity)) return;
  const Index cellCount = k.size();
  if (cellCount == 0) return;
  if (epsilon.size() != cellCount) {
    throw std::runtime_error("computeTurbulentViscosityDevice: k and epsilon differ in length");
  }
  turbulentViscosity.resize(cellCount);

  cfd::Timer timer;
  turbulentViscosityKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, density, cMu, k.data(), epsilon.data(), kFloor, epsilonFloor,
      turbulentViscosity.data());
  checkCuda(cudaGetLastError(), "turbulentViscosityKernel");
  checkCuda(cudaDeviceSynchronize(), "turbulentViscosityKernel sync");
  accountKernel(timer);
}

void computeEffectiveDiffusivityDevice(Real molecularViscosity,
                                       const DeviceBuffer<Real>& turbulentViscosity, Real sigma,
                                       DeviceBuffer<Real>& diffusivity) {
  if (!isfinite(molecularViscosity) || !(molecularViscosity > 0.0)) {
    throw std::runtime_error(
        "computeEffectiveDiffusivityDevice: molecularViscosity must be finite and > 0");
  }
  if (!isfinite(sigma) || !(sigma > 0.0)) {
    throw std::runtime_error("computeEffectiveDiffusivityDevice: sigma must be finite and > 0");
  }
  const Index cellCount = turbulentViscosity.size();
  if (cellCount == 0) return;
  diffusivity.resize(cellCount);

  // P16 Step 4: reused rather than reallocated. A one-int buffer cost one
  // device allocation per call, and this is called several times per
  // turbulence correction -- part of the 30 allocations the production
  // residency ledger measured. Same function-local-static pattern
  // reduceToHost's partialSumsCache already uses. resize(1) after the first
  // call is a no-op, and uploadFrom still clears it to zero every time.
  static DeviceBuffer<int> invalid;
  const int zero = 0;
  invalid.uploadFrom(&zero, 1);

  cfd::Timer timer;
  effectiveDiffusivityKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, molecularViscosity, turbulentViscosity.data(), sigma, diffusivity.data(),
      invalid.data());
  checkCuda(cudaGetLastError(), "effectiveDiffusivityKernel");
  checkCuda(cudaDeviceSynchronize(), "effectiveDiffusivityKernel sync");
  accountKernel(timer);

  int flag = 0;
  invalid.downloadTo(&flag, 1);
  if (flag != 0) {
    throw std::runtime_error(
        "computeEffectiveDiffusivityDevice: every turbulentViscosity entry must be finite and >= "
        "0");
  }
}

void computeKSourceTermsDevice(Real density, const DeviceBuffer<Real>& k,
                               const DeviceBuffer<Real>& epsilon,
                               const DeviceBuffer<Real>& production, Real kFloor,
                               DeviceBuffer<Real>& Su, DeviceBuffer<Real>& Sp) {
  const Index cellCount = k.size();
  if (cellCount == 0) return;
  if (epsilon.size() != cellCount || production.size() != cellCount) {
    throw std::runtime_error(
        "computeKSourceTermsDevice: k, epsilon and production must have one entry per cell");
  }
  Su.resize(cellCount);
  Sp.resize(cellCount);

  cfd::Timer timer;
  kSourceKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(cellCount, density, k.data(),
                                                                epsilon.data(), production.data(),
                                                                kFloor, Su.data(), Sp.data());
  checkCuda(cudaGetLastError(), "kSourceKernel");
  checkCuda(cudaDeviceSynchronize(), "kSourceKernel sync");
  accountKernel(timer);
}

void applyScalarFloorDevice(Real floorValue, DeviceBuffer<Real>& field) {
  const Index cellCount = field.size();
  if (cellCount == 0) return;
  cfd::Timer timer;
  scalarFloorKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(cellCount, floorValue,
                                                                    field.data());
  checkCuda(cudaGetLastError(), "scalarFloorKernel");
  checkCuda(cudaDeviceSynchronize(), "scalarFloorKernel sync");
  accountKernel(timer);
}

void computeEpsilonSourceTermsDevice(Real density, Real c1Epsilon, Real c2Epsilon,
                                     const DeviceBuffer<Real>& k, const DeviceBuffer<Real>& epsilon,
                                     const DeviceBuffer<Real>& production, Real kFloor,
                                     DeviceBuffer<Real>& Su, DeviceBuffer<Real>& Sp) {
  const Index cellCount = k.size();
  if (cellCount == 0) return;
  if (epsilon.size() != cellCount || production.size() != cellCount) {
    throw std::runtime_error(
        "computeEpsilonSourceTermsDevice: k, epsilon and production must have one entry per cell");
  }
  Su.resize(cellCount);
  Sp.resize(cellCount);

  cfd::Timer timer;
  epsilonSourceKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, density, c1Epsilon, c2Epsilon, k.data(), epsilon.data(), production.data(), kFloor,
      Su.data(), Sp.data());
  checkCuda(cudaGetLastError(), "epsilonSourceKernel");
  checkCuda(cudaDeviceSynchronize(), "epsilonSourceKernel sync");
  accountKernel(timer);
}

void computeEffectiveViscosityDevice(Real molecularViscosity,
                                     const DeviceBuffer<Real>& turbulentViscosity,
                                     DeviceBuffer<Real>& effectiveViscosity) {
  // The host throws on exactly this condition (TurbulenceModel.cpp:14-17), and
  // physics::FluidProperties validates the same quantity the same way.
  if (!isfinite(molecularViscosity) || !(molecularViscosity > 0.0)) {
    throw std::runtime_error(
        "computeEffectiveViscosityDevice: molecularViscosity must be finite and > 0");
  }
  const Index cellCount = turbulentViscosity.size();
  if (cellCount == 0) return;
  effectiveViscosity.resize(cellCount);

  // P16 Step 4: reused rather than reallocated. A one-int buffer cost one
  // device allocation per call, and this is called several times per
  // turbulence correction -- part of the 30 allocations the production
  // residency ledger measured. Same function-local-static pattern
  // reduceToHost's partialSumsCache already uses. resize(1) after the first
  // call is a no-op, and uploadFrom still clears it to zero every time.
  static DeviceBuffer<int> invalid;
  const int zero = 0;
  invalid.uploadFrom(&zero, 1);

  cfd::Timer timer;
  effectiveViscosityKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, molecularViscosity, turbulentViscosity.data(), effectiveViscosity.data(),
      invalid.data());
  checkCuda(cudaGetLastError(), "effectiveViscosityKernel");
  checkCuda(cudaDeviceSynchronize(), "effectiveViscosityKernel sync");
  accountKernel(timer);

  int flag = 0;
  invalid.downloadTo(&flag, 1);
  if (flag != 0) {
    throw std::runtime_error(
        "computeEffectiveViscosityDevice: every turbulentViscosity entry must be finite and >= 0");
  }
}

void reassertImposedValuesDevice(const DeviceBuffer<Index>& cells,
                                 const DeviceBuffer<Real>& values, DeviceBuffer<Real>& field) {
  const Index imposedCount = cells.size();
  if (imposedCount == 0) return;
  if (values.size() != imposedCount) {
    throw std::runtime_error(
        "reassertImposedValuesDevice: cell and value buffers differ in length");
  }
  cfd::Timer timer;
  reassertImposedKernel<<<blockCountFor(imposedCount), kThreadsPerBlock>>>(
      imposedCount, cells.data(), values.data(), field.data());
  checkCuda(cudaGetLastError(), "reassertImposedKernel");
  checkCuda(cudaDeviceSynchronize(), "reassertImposedKernel sync");
  accountKernel(timer);
}

void reassertImposedValuesDevice(const DeviceImposedCells& imposed, DeviceBuffer<Real>& field) {
  reassertImposedValuesDevice(imposed.cells, imposed.values, field);
}

void gatherByIndexDevice(const DeviceBuffer<Index>& indices, const DeviceBuffer<Real>& source,
                         DeviceBuffer<Real>& out) {
  const Index count = indices.size();
  if (count == 0) {
    out.resize(0);
    return;
  }
  out.resize(count);
  cfd::Timer timer;
  gatherByIndexKernel<<<blockCountFor(count), kThreadsPerBlock>>>(count, indices.data(),
                                                                  source.data(), out.data());
  checkCuda(cudaGetLastError(), "gatherByIndexKernel");
  checkCuda(cudaDeviceSynchronize(), "gatherByIndexKernel sync");
  accountKernel(timer);
}

}  // namespace cfd::gpu
