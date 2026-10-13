#pragma once

// P18.8 -- the host-side launch interface for the species thermochemistry kernels.
//
// The ARITHMETIC lives in DeviceSpeciesThermochemistry.hpp, which gcc and nvcc both compile. This
// header declares only the launchers, so a CPU-only build can include the arithmetic header (and
// the equivalence tests can run) without needing CUDA at all.
//
// RESIDENCY (sections 36-38): every field below is a DeviceBuffer the CALLER owns and keeps across
// steps. The launchers never allocate, never upload and never download -- so a per-step full-field
// transfer cannot be hidden inside one. The mechanism POD is uploaded ONCE by the caller.

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"

namespace cfd::gpu {

// True when a CUDA device is actually present. Same form as hllcDeviceAvailable() and
// densityBasedDeviceAvailable(): cudaGetDeviceCount returns an error code rather than throwing, so
// a machine with no GPU answers false instead of aborting. The equivalence suite SKIPS on false and
// says so, rather than reporting a pass it did not earn.
[[nodiscard]] bool speciesDeviceAvailable();

// Mixture properties for every cell: M_mix, R_mix, cp_mix, cv_mix, gamma_mix.
//
// `composition` is SPECIES-MAJOR, N_species blocks of N_cells -- the same layout P18.1's
// MassFractionState uses on the host (a std::vector<ScalarField> per species), so the upload is a
// straight copy per species and no transpose is needed anywhere.
void computeMixturePropertiesDevice(const DeviceMechanism& mechanism,
                                    const DeviceBuffer<cfd::Real>& composition,
                                    cfd::Index cellCount, const DeviceSpeciesControls& controls,
                                    DeviceBuffer<cfd::Real>& molecularWeight,
                                    DeviceBuffer<cfd::Real>& gasConstant,
                                    DeviceBuffer<cfd::Real>& specificHeatP,
                                    DeviceBuffer<cfd::Real>& specificHeatV,
                                    DeviceBuffer<cfd::Real>& specificHeatRatio,
                                    DeviceBuffer<int>& status, int threadsPerBlock = 256);

// rho(p,T,Y), p(rho,T,Y) and T(p,rho,Y), each from the per-cell R_mix.
void computeEquationOfStateDevice(
    const DeviceBuffer<cfd::Real>& gasConstant, const DeviceBuffer<cfd::Real>& pressure,
    const DeviceBuffer<cfd::Real>& temperature, const DeviceBuffer<cfd::Real>& density,
    cfd::Index cellCount, DeviceBuffer<cfd::Real>& densityOut, DeviceBuffer<cfd::Real>& pressureOut,
    DeviceBuffer<cfd::Real>& temperatureOut, int threadsPerBlock = 256);

// k(T) and the branch taken, per reaction per cell. The branch is carried out so the equivalence
// suite can compare CONTROL FLOW, not only values -- G9 requires "the control-flow decisions".
void computeRateCoefficientsDevice(const DeviceMechanism& mechanism,
                                   const DeviceBuffer<cfd::Real>& temperature, cfd::Index cellCount,
                                   const DeviceSpeciesControls& controls,
                                   DeviceBuffer<cfd::Real>& rateCoefficient,
                                   DeviceBuffer<int>& branch, DeviceBuffer<int>& status,
                                   int threadsPerBlock = 256);

// Concentrations, progress rates, omega_i, its per-cell sum, and both routes to S_E.
void computeReactionSourcesDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<cfd::Real>& composition,
    const DeviceBuffer<cfd::Real>& density, const DeviceBuffer<cfd::Real>& temperature,
    cfd::Index cellCount, const DeviceSpeciesControls& controls,
    DeviceBuffer<cfd::Real>& concentration, DeviceBuffer<cfd::Real>& progressRate,
    DeviceBuffer<cfd::Real>& omega, DeviceBuffer<cfd::Real>& omegaSum,
    DeviceBuffer<cfd::Real>& heatRelease, DeviceBuffer<cfd::Real>& heatReleaseFromSources,
    DeviceBuffer<int>& status, int threadsPerBlock = 256);

// ONE coupled step of the closed adiabatic reactor, per cell, in P18.6's frozen order. Every
// intermediate is written out, because section 19 and G9 both require comparing them.
void advanceReactorDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<cfd::Real>& composition,
    const DeviceBuffer<cfd::Real>& density, const DeviceBuffer<cfd::Real>& temperature,
    cfd::Index cellCount, cfd::Real dt, cfd::Real minimumTemperature, cfd::Real maximumTemperature,
    const DeviceSpeciesControls& controls, bool staleTemperature, bool staleComposition,
    cfd::Real staleTemperatureValue, DeviceBuffer<cfd::Real>& compositionOut,
    DeviceBuffer<cfd::Real>& temperatureOut, DeviceBuffer<cfd::Real>& progressRateOut,
    DeviceBuffer<cfd::Real>& rateCoefficientOut, DeviceBuffer<cfd::Real>& omegaOut,
    DeviceBuffer<cfd::Real>& omegaSumOut, DeviceBuffer<cfd::Real>& heatReleaseOut,
    DeviceBuffer<cfd::Real>& sensibleBeforeOut, DeviceBuffer<cfd::Real>& sensibleAfterOut,
    DeviceBuffer<cfd::Real>& chemicalBeforeOut, DeviceBuffer<cfd::Real>& chemicalAfterOut,
    DeviceBuffer<int>& branchOut, DeviceBuffer<int>& statusOut, int threadsPerBlock = 256);

}  // namespace cfd::gpu
