// P18.8 amendment A2 -- the --fmad=true non-vacuity control. See
// include/cfd/gpu/DeviceSpeciesFmadControl.hpp for why it exists and why it is never a solver path.
//
// The kernel bodies below are the production ones from DeviceSpeciesKernel.cu, unchanged. They have
// to be, or the comparison would attribute to the edit instead of to the flag. The ONE difference
// between this translation unit and that one is the COMPILE_OPTIONS property in
// cuda/CMakeLists.txt: --fmad=true here, --fmad=false there.
//
// This is the same control P17.7 ran on the HLLC kernel, where it recorded 8 of 17 rows bitwise and
// max |flux diff| 1.52587890625e-04 (cuda/CMakeLists.txt:133). Reusing the established shape rather
// than inventing one.

#include <cuda_runtime.h>

#include <cmath>

#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceSpeciesFmadControl.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

int blockCountFor(Index count, int threadsPerBlock) {
  return static_cast<int>((count + threadsPerBlock - 1) / threadsPerBlock);
}

__device__ inline void gatherComposition(const Real* composition, Index cellCount, Index cell,
                                         Index speciesCount, Real* out) {
  for (Index i = 0; i < speciesCount; ++i) {
    out[i] = composition[(i * cellCount) + cell];
  }
}

__global__ void mixturePropertiesFmadKernel(DeviceMechanism mechanism, const Real* composition,
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

__global__ void reactionSourcesFmadKernel(DeviceMechanism mechanism, const Real* composition,
                                          const Real* density, const Real* temperature,
                                          Index cellCount, DeviceSpeciesControls controls,
                                          Real* concentration, Real* progressRate, Real* omega,
                                          Real* omegaSum, Real* heatRelease,
                                          Real* heatReleaseFromSources, int* status) {
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

// a*b + c, with the operands read from memory so the compiler cannot fold the expression at compile
// time and answer with host arithmetic instead of the flag under test.
__global__ void fmadDetectKernel(const Real* in, Real* out) { out[0] = in[0] * in[1] + in[2]; }

}  // namespace

bool fmadControlIsReallyContracting() {
  // A DYADIC discriminator, exact in both routes, chosen so the two answers differ:
  //   a = b = 1 + 2^-27   =>  a*b = 1 + 2^-26 + 2^-54 exactly
  //   rounded to double, a*b loses the 2^-54 term (it is below half an ulp of 1), giving 1 + 2^-26
  //   c = -(1 + 2^-26)
  // Separate multiply then add:  (1 + 2^-26) - (1 + 2^-26)  =  EXACTLY 0
  // Fused multiply-add:          (1 + 2^-26 + 2^-54) - (1 + 2^-26)  =  2^-54, nonzero
  // So a nonzero result means and can only mean that the compiler fused it.
  const Real a = 1.0 + ::ldexp(1.0, -27);
  const Real c = -(1.0 + ::ldexp(1.0, -26));
  const Real host[3] = {a, a, c};

  DeviceBuffer<Real> in(3);
  in.uploadFrom(host, 3);
  DeviceBuffer<Real> out(1);
  fmadDetectKernel<<<1, 1>>>(in.data(), out.data());
  checkCuda(cudaGetLastError(), "fmadDetectKernel");
  checkCuda(cudaDeviceSynchronize(), "fmadDetectKernel sync");
  Real result = 0.0;
  out.downloadTo(&result, 1);
  return result != 0.0;
}

void computeMixturePropertiesFmadControlDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<Real>& composition, Index cellCount,
    const DeviceSpeciesControls& controls, DeviceBuffer<Real>& molecularWeight,
    DeviceBuffer<Real>& gasConstant, DeviceBuffer<Real>& specificHeatP,
    DeviceBuffer<Real>& specificHeatV, DeviceBuffer<Real>& specificHeatRatio,
    DeviceBuffer<int>& status, int threadsPerBlock) {
  if (cellCount <= 0) {
    return;
  }
  mixturePropertiesFmadKernel<<<blockCountFor(cellCount, threadsPerBlock), threadsPerBlock>>>(
      mechanism, composition.data(), cellCount, controls, molecularWeight.data(),
      gasConstant.data(), specificHeatP.data(), specificHeatV.data(), specificHeatRatio.data(),
      status.data());
  checkCuda(cudaGetLastError(), "mixturePropertiesFmadKernel");
  checkCuda(cudaDeviceSynchronize(), "mixturePropertiesFmadKernel sync");
}

void computeReactionSourcesFmadControlDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<Real>& composition,
    const DeviceBuffer<Real>& density, const DeviceBuffer<Real>& temperature, Index cellCount,
    const DeviceSpeciesControls& controls, DeviceBuffer<Real>& concentration,
    DeviceBuffer<Real>& progressRate, DeviceBuffer<Real>& omega, DeviceBuffer<Real>& omegaSum,
    DeviceBuffer<Real>& heatRelease, DeviceBuffer<Real>& heatReleaseFromSources,
    DeviceBuffer<int>& status, int threadsPerBlock) {
  if (cellCount <= 0) {
    return;
  }
  reactionSourcesFmadKernel<<<blockCountFor(cellCount, threadsPerBlock), threadsPerBlock>>>(
      mechanism, composition.data(), density.data(), temperature.data(), cellCount, controls,
      concentration.data(), progressRate.data(), omega.data(), omegaSum.data(), heatRelease.data(),
      heatReleaseFromSources.data(), status.data());
  checkCuda(cudaGetLastError(), "reactionSourcesFmadKernel");
  checkCuda(cudaDeviceSynchronize(), "reactionSourcesFmadKernel sync");
}

}  // namespace cfd::gpu
