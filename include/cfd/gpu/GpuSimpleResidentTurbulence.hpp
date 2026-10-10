#pragma once

// P16-GPU-TURB Stage 7I -- the resident flow state the turbulence correction
// must read, exposed WITHOUT downloading it.
//
// CUDA-only. See DeviceBuffer.hpp's "never include from a CPU-only-compiled
// file" rule. This is the definition the friend declaration in
// GpuSimpleDiscretization.hpp deliberately leaves opaque.
//
// WHICH STATE, AND WHY IT IS THIS ONE
// -----------------------------------
// The audited CPU ordering (SIMPLE.cpp:537-547) runs the turbulence correction
// FIRST in outer iteration N, against the PRESSURE-CORRECTED velocity from the
// end of iteration N-1. Not the momentum predictor, not this iteration's
// corrected velocity -- the code calls it "the same lagged-current-state
// convention this loop already applies to boundary conditions".
//
// On the resident path that state is `Impl::velocity` and `Impl::massFlux`,
// because beginIterationResident says so in its own words:
//
//     "Nothing crosses PCIe. The corrections already carried their results
//      into `velocity` and `massFlux` ... this is the explicit statement that
//      a new outer iteration begins from them."
//
// So the buffers below ARE the CPU-equivalent state. The host mirror is not:
// a resident loop stops downloading velocity, so the host field is frozen at
// iteration 0 for the whole solve. That is the invariant the laminar guard has
// been protecting, and it is a statement about the HOST field rather than
// about k-epsilon.
//
// BORROWED, NOT OWNED. Every pointer here aliases buffers owned by
// GpuSimpleDiscretization::Impl. The view must not outlive the discretization
// it came from, and nothing here frees anything -- the same contract
// DeviceCsrMatrix::adoptDevice already carries.

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"

namespace cfd::gpu {

struct ResidentTurbulenceInputs {
  // The corrected velocity carried out of the previous outer iteration.
  const DeviceVelocity* velocity{nullptr};
  // SIMPLE's CARRIED CORRECTED face flux -- the pressure-corrected,
  // Rhie-Chow one the continuity algorithm carries between outer iterations.
  //
  // IT IS NOT THE TURBULENCE CONVECTION FLUX, and an earlier version of this
  // comment wrongly said it was "exactly the flux the CPU turbulence
  // correction receives". The CPU correction receives no flux at all:
  // KEpsilonModel::correctOnHost calls calculateMassFlux(mesh, velocity, ...)
  // and builds its own from the cell velocity. The two differ by the Rhie-Chow
  // face correction -- measured at 3.231e-06 relative on the validated channel,
  // which is the same order as the k and mu_t discrepancy it produced.
  //
  // Retained here because the resident pressure/continuity path legitimately
  // uses it. The turbulence convection flux is built separately, from
  // `fluxPlan` below. Do not conflate the two.
  const DeviceBuffer<cfd::Real>* massFlux{nullptr};

  // The plan calculateMassFluxDevice needs to rebuild the TURBULENCE
  // convection flux from the resident velocity -- the device port of the same
  // physics::calculateMassFlux the CPU reference calls, so both paths build
  // the same quantity from the same velocity.
  //
  // Borrowed, like everything else here.
  const DeviceFaceFluxPlan* fluxPlan{nullptr};
  // The plan the already-qualified computeVelocityGradientDevice needs. Reused
  // rather than rebuilt: it encodes the velocity boundary conditions, and a
  // second plan built from the same data would be a second chance to get them
  // wrong.
  const DeviceMomentumConvectionPlan* convectionPlan{nullptr};
  cfd::Index cellCount{};

  [[nodiscard]] bool usable() const noexcept {
    return velocity != nullptr && massFlux != nullptr && convectionPlan != nullptr &&
           fluxPlan != nullptr && cellCount > 0;
  }
};

// Borrows the resident state. Returns an unusable view when the discretization
// has no resident state yet (uploadInitialState has not run), so a caller
// cannot mistake "not started" for "empty".
[[nodiscard]] ResidentTurbulenceInputs residentTurbulenceInputs(
    GpuSimpleDiscretization& discretization);

// P16 Phase 1 -- the return direction: mu_eff = mu + mu_t, computed on device
// straight into the discretization's own resident viscosity buffer.
//
// WHAT IT REPLACES. Today the loop runs
//
//     device mu_t --D2H--> host mu_t --effectiveViscosity()--> host mu_eff
//                                            --H2D--> device viscosity
//
// every outer iteration in which the turbulence model changed anything, which
// with turbulence active is every iteration: 8N down and 8N up for a field the
// device already had. This is the same defect shape as the wall-production
// round trip removed earlier, one level up the call stack.
//
// OWNERSHIP. On success the discretization's viscosity becomes
// FieldAuthority::DeviceOwned: the device copy is authoritative and any host
// copy is stale, which is the existing vocabulary for exactly this situation
// rather than a new one. The caller keeps ownership of `turbulentViscosity`;
// this reads it and writes the discretization's own buffer. Nothing is aliased
// and nothing outlives the call.
//
// It does NOT touch the laminar resident guard and does not make turbulent
// resident momentum reachable -- the wall-face viscosity override that would
// require does not exist on the device yet
// (results/p16/turbulence/gpu_equivalence/viscosity_path/audit.md).
//
// Returns false when the discretization has no resident state yet, for the same
// reason residentTurbulenceInputs returns an unusable view: a caller must not
// mistake "not started" for "done".
struct ResidentTurbulentViscosity {
  // BORROWED, not owned -- the same contract ResidentTurbulenceInputs carries.
  // This is the turbulence model's own device mu_t; the call reads it and
  // writes the discretization's viscosity buffer.
  const DeviceBuffer<cfd::Real>* turbulentViscosity{nullptr};
  cfd::Real molecularViscosity{};

  [[nodiscard]] bool usable() const noexcept {
    return turbulentViscosity != nullptr && molecularViscosity > 0.0;
  }
};

bool adoptDeviceEffectiveViscosity(GpuSimpleDiscretization& discretization,
                                   const ResidentTurbulentViscosity& turbulent);

}  // namespace cfd::gpu
