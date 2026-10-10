// P18.8 -- the first species and chemistry physics on the device.
//
// WHAT IS NEW HERE AND WHAT IS NOT. Every formula comes from
// include/cfd/gpu/DeviceSpeciesThermochemistry.hpp, which gcc and nvcc BOTH compile -- so none of
// the arithmetic is written twice and the kernels below are launch geometry and nothing else. That
// is G9's condition (1), and it is the reason the equivalence tests can also run in a CPU-only
// build.
//
// COMPILED WITH --fmad=false, registered in cuda/CMakeLists.txt alongside the twenty-odd existing
// kernels. nvcc fuses a*b+c into one FMA by default and the host SSE2 baseline does not; since the
// arithmetic is ONE source compiled for both, that difference -- and nothing else -- would break
// the bitwise equality G9 requires.
//
// NO ATOMICS AND NO REDUCTIONS. Every kernel is one thread per cell, writing only that cell's
// outputs, so there is no cross-thread accumulation whose order could vary. That is why G9's
// conditions (2) and (5) are satisfied vacuously rather than by effort, and it is also why the
// block-size determinism check (section 46) is expected to be exact rather than merely close.

#include <cuda_runtime.h>

#include <cmath>

#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceSpeciesLaunch.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

int blockCountFor(Index count, int threadsPerBlock) {
  return static_cast<int>((count + threadsPerBlock - 1) / threadsPerBlock);
}

// Gathers one cell's composition out of the SPECIES-MAJOR layout into a small local array, so the
// shared arithmetic sees exactly the contiguous vector the host version does.
__device__ inline void gatherComposition(const Real* composition, Index cellCount, Index cell,
                                         Index speciesCount, Real* out) {
  for (Index i = 0; i < speciesCount; ++i) {
    out[i] = composition[(i * cellCount) + cell];
  }
}

__global__ void mixturePropertiesKernel(DeviceMechanism mechanism, const Real* composition,
                                        Index cellCount, DeviceSpeciesControls controls,
                                        Real* molecularWeight, Real* gasConstant,
                                        Real* specificHeatP, Real* specificHeatV,
                                        Real* specificHeatRatio, int* status) {
  const Index cell = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (cell >= cellCount) {
    return;
  }
  Real local[kMaxDeviceSpecies];
  gatherComposition(composition, cellCount, cell, mechanism.speciesCount, local);

  DeviceSpeciesStatus cellStatus = DeviceSpeciesStatus::Ok;
  molecularWeight[cell] = deviceMolecularWeight(mechanism, local, controls, &cellStatus);
  gasConstant[cell] = deviceGasConstant(mechanism, local, controls);
  specificHeatP[cell] = deviceSpecificHeatPressure(mechanism, local, controls);
  specificHeatV[cell] = deviceSpecificHeatVolume(specificHeatP[cell], gasConstant[cell]);
  specificHeatRatio[cell] = deviceSpecificHeatRatio(specificHeatP[cell], specificHeatV[cell]);
  if (!(specificHeatV[cell] > 0.0) && cellStatus == DeviceSpeciesStatus::Ok) {
    cellStatus = DeviceSpeciesStatus::NonPositiveSpecificHeatVolume;
  }
  status[cell] = static_cast<int>(cellStatus);
}

__global__ void equationOfStateKernel(const Real* gasConstant, const Real* pressure,
                                      const Real* temperature, const Real* density, Index cellCount,
                                      Real* densityOut, Real* pressureOut, Real* temperatureOut) {
  const Index cell = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (cell >= cellCount) {
    return;
  }
  densityOut[cell] =
      deviceDensityFromPressure(pressure[cell], gasConstant[cell], temperature[cell]);
  pressureOut[cell] = devicePressure(density[cell], gasConstant[cell], temperature[cell]);
  temperatureOut[cell] =
      deviceTemperatureFromPressure(pressure[cell], density[cell], gasConstant[cell]);
}

__global__ void rateCoefficientKernel(DeviceMechanism mechanism, const Real* temperature,
                                      Index cellCount, DeviceSpeciesControls controls,
                                      Real* rateCoefficient, int* branch, int* status) {
  const Index cell = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (cell >= cellCount) {
    return;
  }
  DeviceSpeciesStatus cellStatus = DeviceSpeciesStatus::Ok;
  const Real t = temperature[cell];
  if (!::isfinite(t)) {
    cellStatus = DeviceSpeciesStatus::NonFiniteTemperature;
  } else if (!(t > 0.0)) {
    cellStatus = DeviceSpeciesStatus::NonPositiveTemperature;
  }
  for (Index r = 0; r < mechanism.reactionCount; ++r) {
    const Index slot = (r * cellCount) + cell;
    if (cellStatus != DeviceSpeciesStatus::Ok && !controls.acceptInvalidState) {
      rateCoefficient[slot] = 0.0;
      branch[slot] = -1;
      continue;
    }
    int taken = -1;
    rateCoefficient[slot] =
        deviceRateCoefficient(mechanism.forward[r], t, controls, &cellStatus, &taken);
    branch[slot] = taken;
  }
  status[cell] = static_cast<int>(cellStatus);
}

__global__ void reactionSourcesKernel(DeviceMechanism mechanism, const Real* composition,
                                      const Real* density, const Real* temperature, Index cellCount,
                                      DeviceSpeciesControls controls, Real* concentration,
                                      Real* progressRate, Real* omega, Real* omegaSum,
                                      Real* heatRelease, Real* heatReleaseFromSources,
                                      int* status) {
  const Index cell = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (cell >= cellCount) {
    return;
  }
  Real local[kMaxDeviceSpecies];
  gatherComposition(composition, cellCount, cell, mechanism.speciesCount, local);

  DeviceSpeciesStatus cellStatus = DeviceSpeciesStatus::Ok;
  Real localConcentration[kMaxDeviceSpecies];
  Real localRates[kMaxDeviceReactions];
  Real localOmega[kMaxDeviceSpecies];

  const Real rho = density[cell];
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    localConcentration[i] = deviceConcentration(mechanism, i, rho, local[i]);
    concentration[(i * cellCount) + cell] = localConcentration[i];
  }
  for (Index r = 0; r < mechanism.reactionCount; ++r) {
    localRates[r] = deviceProgressRate(mechanism, r, localConcentration, temperature[cell],
                                       controls, &cellStatus);
    progressRate[(r * cellCount) + cell] = localRates[r];
  }
  deviceSpeciesSources(mechanism, localRates, localOmega, controls);
  Real sum = 0.0;
  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    omega[(i * cellCount) + cell] = localOmega[i];
    sum += localOmega[i];
  }
  omegaSum[cell] = sum;
  heatRelease[cell] = deviceHeatRelease(mechanism, localRates, controls, &cellStatus);
  heatReleaseFromSources[cell] = deviceHeatReleaseFromSources(mechanism, localOmega);
  status[cell] = static_cast<int>(cellStatus);
}

__global__ void advanceReactorKernel(
    DeviceMechanism mechanism, const Real* composition, const Real* density,
    const Real* temperature, Index cellCount, Real dt, Real minimumTemperature,
    Real maximumTemperature, DeviceSpeciesControls controls, bool staleTemperature,
    bool staleComposition, Real staleTemperatureValue, Real* compositionOut, Real* temperatureOut,
    Real* progressRateOut, Real* rateCoefficientOut, Real* omegaOut, Real* omegaSumOut,
    Real* heatReleaseOut, Real* sensibleBeforeOut, Real* sensibleAfterOut, Real* chemicalBeforeOut,
    Real* chemicalAfterOut, int* branchOut, int* statusOut) {
  const Index cell = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (cell >= cellCount) {
    return;
  }
  Real local[kMaxDeviceSpecies];
  gatherComposition(composition, cellCount, cell, mechanism.speciesCount, local);

  DeviceReactorStep step;
  deviceAdvanceReactorCell(mechanism, local, density[cell], temperature[cell], dt,
                           minimumTemperature, maximumTemperature, controls, staleTemperature,
                           staleComposition, staleTemperatureValue, &step);

  for (Index i = 0; i < mechanism.speciesCount; ++i) {
    compositionOut[(i * cellCount) + cell] = step.compositionAfter[i];
    omegaOut[(i * cellCount) + cell] = step.omega[i];
  }
  for (Index r = 0; r < mechanism.reactionCount; ++r) {
    progressRateOut[(r * cellCount) + cell] = step.progressRate[r];
    rateCoefficientOut[(r * cellCount) + cell] = step.rateCoefficient[r];
    branchOut[(r * cellCount) + cell] = step.branch[r];
  }
  temperatureOut[cell] = step.temperatureAfter;
  omegaSumOut[cell] = step.omegaSum;
  heatReleaseOut[cell] = step.heatRelease;
  sensibleBeforeOut[cell] = step.sensibleBefore;
  sensibleAfterOut[cell] = step.sensibleAfter;
  chemicalBeforeOut[cell] = step.chemicalBefore;
  chemicalAfterOut[cell] = step.chemicalAfter;
  statusOut[cell] = static_cast<int>(step.status);
}

}  // namespace

bool speciesDeviceAvailable() {
  int deviceCount = 0;
  const cudaError_t status = cudaGetDeviceCount(&deviceCount);
  return status == cudaSuccess && deviceCount > 0;
}

void computeMixturePropertiesDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<Real>& composition, Index cellCount,
    const DeviceSpeciesControls& controls, DeviceBuffer<Real>& molecularWeight,
    DeviceBuffer<Real>& gasConstant, DeviceBuffer<Real>& specificHeatP,
    DeviceBuffer<Real>& specificHeatV, DeviceBuffer<Real>& specificHeatRatio,
    DeviceBuffer<int>& status, int threadsPerBlock) {
  if (cellCount <= 0) {
    return;
  }
  mixturePropertiesKernel<<<blockCountFor(cellCount, threadsPerBlock), threadsPerBlock>>>(
      mechanism, composition.data(), cellCount, controls, molecularWeight.data(),
      gasConstant.data(), specificHeatP.data(), specificHeatV.data(), specificHeatRatio.data(),
      status.data());
  checkCuda(cudaGetLastError(), "mixturePropertiesKernel");
  checkCuda(cudaDeviceSynchronize(), "mixturePropertiesKernel sync");
}

void computeEquationOfStateDevice(const DeviceBuffer<Real>& gasConstant,
                                  const DeviceBuffer<Real>& pressure,
                                  const DeviceBuffer<Real>& temperature,
                                  const DeviceBuffer<Real>& density, Index cellCount,
                                  DeviceBuffer<Real>& densityOut, DeviceBuffer<Real>& pressureOut,
                                  DeviceBuffer<Real>& temperatureOut, int threadsPerBlock) {
  if (cellCount <= 0) {
    return;
  }
  equationOfStateKernel<<<blockCountFor(cellCount, threadsPerBlock), threadsPerBlock>>>(
      gasConstant.data(), pressure.data(), temperature.data(), density.data(), cellCount,
      densityOut.data(), pressureOut.data(), temperatureOut.data());
  checkCuda(cudaGetLastError(), "equationOfStateKernel");
  checkCuda(cudaDeviceSynchronize(), "equationOfStateKernel sync");
}

void computeRateCoefficientsDevice(const DeviceMechanism& mechanism,
                                   const DeviceBuffer<Real>& temperature, Index cellCount,
                                   const DeviceSpeciesControls& controls,
                                   DeviceBuffer<Real>& rateCoefficient, DeviceBuffer<int>& branch,
                                   DeviceBuffer<int>& status, int threadsPerBlock) {
  if (cellCount <= 0) {
    return;
  }
  rateCoefficientKernel<<<blockCountFor(cellCount, threadsPerBlock), threadsPerBlock>>>(
      mechanism, temperature.data(), cellCount, controls, rateCoefficient.data(), branch.data(),
      status.data());
  checkCuda(cudaGetLastError(), "rateCoefficientKernel");
  checkCuda(cudaDeviceSynchronize(), "rateCoefficientKernel sync");
}

void computeReactionSourcesDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<Real>& composition,
    const DeviceBuffer<Real>& density, const DeviceBuffer<Real>& temperature, Index cellCount,
    const DeviceSpeciesControls& controls, DeviceBuffer<Real>& concentration,
    DeviceBuffer<Real>& progressRate, DeviceBuffer<Real>& omega, DeviceBuffer<Real>& omegaSum,
    DeviceBuffer<Real>& heatRelease, DeviceBuffer<Real>& heatReleaseFromSources,
    DeviceBuffer<int>& status, int threadsPerBlock) {
  if (cellCount <= 0) {
    return;
  }
  reactionSourcesKernel<<<blockCountFor(cellCount, threadsPerBlock), threadsPerBlock>>>(
      mechanism, composition.data(), density.data(), temperature.data(), cellCount, controls,
      concentration.data(), progressRate.data(), omega.data(), omegaSum.data(), heatRelease.data(),
      heatReleaseFromSources.data(), status.data());
  checkCuda(cudaGetLastError(), "reactionSourcesKernel");
  checkCuda(cudaDeviceSynchronize(), "reactionSourcesKernel sync");
}

void advanceReactorDevice(const DeviceMechanism& mechanism, const DeviceBuffer<Real>& composition,
                          const DeviceBuffer<Real>& density, const DeviceBuffer<Real>& temperature,
                          Index cellCount, Real dt, Real minimumTemperature,
                          Real maximumTemperature, const DeviceSpeciesControls& controls,
                          bool staleTemperature, bool staleComposition, Real staleTemperatureValue,
                          DeviceBuffer<Real>& compositionOut, DeviceBuffer<Real>& temperatureOut,
                          DeviceBuffer<Real>& progressRateOut,
                          DeviceBuffer<Real>& rateCoefficientOut, DeviceBuffer<Real>& omegaOut,
                          DeviceBuffer<Real>& omegaSumOut, DeviceBuffer<Real>& heatReleaseOut,
                          DeviceBuffer<Real>& sensibleBeforeOut,
                          DeviceBuffer<Real>& sensibleAfterOut,
                          DeviceBuffer<Real>& chemicalBeforeOut,
                          DeviceBuffer<Real>& chemicalAfterOut, DeviceBuffer<int>& branchOut,
                          DeviceBuffer<int>& statusOut, int threadsPerBlock) {
  if (cellCount <= 0) {
    return;
  }
  advanceReactorKernel<<<blockCountFor(cellCount, threadsPerBlock), threadsPerBlock>>>(
      mechanism, composition.data(), density.data(), temperature.data(), cellCount, dt,
      minimumTemperature, maximumTemperature, controls, staleTemperature, staleComposition,
      staleTemperatureValue, compositionOut.data(), temperatureOut.data(), progressRateOut.data(),
      rateCoefficientOut.data(), omegaOut.data(), omegaSumOut.data(), heatReleaseOut.data(),
      sensibleBeforeOut.data(), sensibleAfterOut.data(), chemicalBeforeOut.data(),
      chemicalAfterOut.data(), branchOut.data(), statusOut.data());
  checkCuda(cudaGetLastError(), "advanceReactorKernel");
  checkCuda(cudaDeviceSynchronize(), "advanceReactorKernel sync");
}

}  // namespace cfd::gpu
