// P17.7 -- the device side of the HLLC flux.
//
// THERE IS NO PHYSICS IN THIS FILE. Every formula is in include/cfd/compressible/HllcFlux.hpp,
// marked CFD_HLLC_HOST_DEVICE, and this translation unit calls it. That is the whole point: the
// authorization's section 28 asks to avoid copy-pasted mathematical logic where a shared host/device
// implementation is practical, and for a pointwise flux it is. Search this file for a wave speed, a
// star state or an EOS and you will not find one.
//
// Compiled with --fmad=false (see cuda/CMakeLists.txt) so that nvcc does not fuse a*b+c into an FMA
// where the host build does not, which is the only thing that would break bitwise equality for a
// reason unrelated to the physics.

#include <cuda_runtime.h>

#include <string>

#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/gpu/DeviceHllcFlux.hpp"

namespace cfd::gpu {

namespace {

using cfd::compressible::HllcResult;
using cfd::compressible::WaveSpeedEstimator;

__device__ void pack(const HllcResult& r, HllcDeviceOutput& o) {
  o.flux[0] = r.flux.mass;
  o.flux[1] = r.flux.momentum.x;
  o.flux[2] = r.flux.momentum.y;
  o.flux[3] = r.flux.momentum.z;
  o.flux[4] = r.flux.energy;
  o.sLeft = r.speeds.left;
  o.sContact = r.speeds.contact;
  o.sRight = r.speeds.right;
  o.denominator = r.speeds.denominator;
  o.starDensityLeft = r.starDensityLeft;
  o.starDensityRight = r.starDensityRight;
  o.starInternalEnergyLeft = r.starInternalEnergyLeft;
  o.starInternalEnergyRight = r.starInternalEnergyRight;
  o.branch = static_cast<int>(r.branch);
  o.validity = static_cast<int>(r.validity);
  o.valid = r.valid ? 1 : 0;
}

__global__ void hllcKernel(const HllcDeviceInput* in, HllcDeviceOutput* out, int count) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  const HllcDeviceInput e = in[i];
  const WaveSpeedEstimator est =
      e.estimator == 1 ? WaveSpeedEstimator::Davis : WaveSpeedEstimator::ToroPressureBased;
  // THE SHARED IMPLEMENTATION, called verbatim.
  pack(cfd::compressible::hllcFlux(e.left, e.right, e.normal, e.gamma, est), out[i]);
}

__global__ void hllKernel(const HllcDeviceInput* in, HllcDeviceOutput* out, int count) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  const HllcDeviceInput e = in[i];
  const WaveSpeedEstimator est =
      e.estimator == 1 ? WaveSpeedEstimator::Davis : WaveSpeedEstimator::ToroPressureBased;
  pack(cfd::compressible::hllFluxForControlOnly(e.left, e.right, e.normal, e.gamma, est), out[i]);
}

bool fail(std::string* message, const char* what, cudaError_t status) {
  if (message != nullptr) {
    *message = std::string(what) + ": " + cudaGetErrorString(status);
  }
  return false;
}

template <typename Kernel>
bool launch(Kernel kernel, const HllcDeviceInput* inputs, HllcDeviceOutput* outputs, int count,
            std::string* message) {
  if (!hllcDeviceAvailable()) {
    if (message != nullptr) *message = "no CUDA device available";
    return false;
  }
  if (count <= 0) {
    if (message != nullptr) *message = "count must be > 0";
    return false;
  }

  HllcDeviceInput* dIn = nullptr;
  HllcDeviceOutput* dOut = nullptr;
  const size_t inBytes = static_cast<size_t>(count) * sizeof(HllcDeviceInput);
  const size_t outBytes = static_cast<size_t>(count) * sizeof(HllcDeviceOutput);

  cudaError_t status = cudaMalloc(&dIn, inBytes);
  if (status != cudaSuccess) return fail(message, "cudaMalloc(inputs)", status);
  status = cudaMalloc(&dOut, outBytes);
  if (status != cudaSuccess) {
    cudaFree(dIn);
    return fail(message, "cudaMalloc(outputs)", status);
  }

  status = cudaMemcpy(dIn, inputs, inBytes, cudaMemcpyHostToDevice);
  if (status == cudaSuccess) {
    const int threads = 128;
    const int blocks = (count + threads - 1) / threads;
    kernel<<<blocks, threads>>>(dIn, dOut, count);
    status = cudaGetLastError();
    if (status == cudaSuccess) status = cudaDeviceSynchronize();
    if (status == cudaSuccess) {
      status = cudaMemcpy(outputs, dOut, outBytes, cudaMemcpyDeviceToHost);
    }
  }

  cudaFree(dIn);
  cudaFree(dOut);
  if (status != cudaSuccess) return fail(message, "hllc kernel", status);
  if (message != nullptr) message->clear();
  return true;
}

}  // namespace

bool hllcDeviceAvailable() {
  int deviceCount = 0;
  const cudaError_t status = cudaGetDeviceCount(&deviceCount);
  return status == cudaSuccess && deviceCount > 0;
}

bool evaluateHllcOnDevice(const HllcDeviceInput* inputs, HllcDeviceOutput* outputs, int count,
                          std::string* message) {
  return launch(hllcKernel, inputs, outputs, count, message);
}

bool evaluateHllOnDeviceForControlOnly(const HllcDeviceInput* inputs, HllcDeviceOutput* outputs,
                                      int count, std::string* message) {
  return launch(hllKernel, inputs, outputs, count, message);
}

}  // namespace cfd::gpu
