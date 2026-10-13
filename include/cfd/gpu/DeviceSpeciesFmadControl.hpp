#pragma once

// P18.8 amendment A2 -- THE NON-VACUITY CONTROL FOR G9 CONDITION (4), and nothing else.
//
// WHY THIS FILE EXISTS. A2 replaces the unachievable BITWISE criterion on the four
// transcendental-dependent quantities with a derived ULP bound, and keeps BITWISE everywhere else.
// A bound is only worth stating if something can violate it, and the thing it must not hide is FMA
// contraction: nvcc fuses a*b+c into one FMA by default, the host SSE2 baseline does not, and
// --fmad=false is what makes the two agree. If a replacement criterion were loose enough to pass
// with FMA contraction present, it would have abandoned G9's condition (4) silently.
//
// So the control is the SAME shared arithmetic header compiled into a SECOND translation unit with
// --fmad=TRUE, registered in cuda/CMakeLists.txt. Same source, same operand order, same
// parenthesisation -- the compile flag is the only difference, which is what makes the comparison
// attribute to FMA and to nothing else.
//
// IT IS NEVER CALLED BY PRODUCTION. The only caller is the equivalence suite. Every name carries
// FmadControl so a reader of a stack trace or a grep cannot mistake it for a solver path.
//
// Only the two kernels carrying contractable accumulations are duplicated. The shared header has
// six a*b+c sites: R_mix, cp_mix, omega_i, Delta H_r, S_E and the chemical energy pool. It also has
// one accumulation FMA CANNOT touch -- deviceMolecularWeight's `reciprocal += composition[i] /
// weight`, a divide then an add. That asymmetry is the control's own self-check: under --fmad=true
// R_mix and cp_mix must break while M_mix stays bitwise. A control that broke all three, or none,
// would be measuring something other than contraction.

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"

namespace cfd::gpu {

// Same signature and same semantics as computeMixturePropertiesDevice, compiled with --fmad=true.
void computeMixturePropertiesFmadControlDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<cfd::Real>& composition,
    cfd::Index cellCount, const DeviceSpeciesControls& controls,
    DeviceBuffer<cfd::Real>& molecularWeight, DeviceBuffer<cfd::Real>& gasConstant,
    DeviceBuffer<cfd::Real>& specificHeatP, DeviceBuffer<cfd::Real>& specificHeatV,
    DeviceBuffer<cfd::Real>& specificHeatRatio, DeviceBuffer<int>& status,
    int threadsPerBlock = 256);

// Same signature and same semantics as computeReactionSourcesDevice, compiled with --fmad=true.
void computeReactionSourcesFmadControlDevice(
    const DeviceMechanism& mechanism, const DeviceBuffer<cfd::Real>& composition,
    const DeviceBuffer<cfd::Real>& density, const DeviceBuffer<cfd::Real>& temperature,
    cfd::Index cellCount, const DeviceSpeciesControls& controls,
    DeviceBuffer<cfd::Real>& concentration, DeviceBuffer<cfd::Real>& progressRate,
    DeviceBuffer<cfd::Real>& omega, DeviceBuffer<cfd::Real>& omegaSum,
    DeviceBuffer<cfd::Real>& heatRelease, DeviceBuffer<cfd::Real>& heatReleaseFromSources,
    DeviceBuffer<int>& status, int threadsPerBlock = 256);

// True when the control translation unit really was compiled with FMA contraction enabled.
//
// It evaluates, ON THE DEVICE, a dyadic triple (a, b, c) for which a*b+c and fma(a,b,c) differ in
// the last bit, and reports whether the compiler fused it. Without this the control could be
// reporting "no difference" because cuda/CMakeLists.txt lost the --fmad=true property, which is a
// fail-open probe of exactly the shape that has bitten this project before: a silent pass that
// looks like the feature being absent.
[[nodiscard]] bool fmadControlIsReallyContracting();

}  // namespace cfd::gpu
