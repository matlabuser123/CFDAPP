#pragma once

// P16-GPU-TURB -- CUDA-only. See DeviceBuffer.hpp's "never include from a
// CPU-only-compiled file" rule.
//
// The first turbulence physics that actually executes on the device:
// the strain-rate invariant, the production term, and the eddy viscosity.
//
// WHAT THIS REUSES, AND WHAT IS NEW
// ---------------------------------
// The audit found the velocity gradient ALREADY PORTED and qualified:
// `computeVelocityGradientDevice` (DeviceMomentumConvection.hpp) is described
// by its own header as the "CUDA port of discretization::computeVelocityGradient
// (GreenGauss branch)", exposed so a differential can compare it directly. The
// production k-epsilon path calls `computeVelocityGradient(mesh, velocity,
// velocityBoundaries_)` with the default GreenGauss scheme, so it is the same
// operator. This file therefore does NOT re-port gradients -- the same shape as
// P14, where the two hard halves already existed.
//
// Genuinely new here are the three cell-local algebraic steps:
//
//   S^2    = 2(du/dx)^2 + 2(dv/dy)^2 + (du/dy + dv/dx)^2
//   P_k    = mu_t * S^2
//   mu_t   = rho C_mu kSafe^2 / epsSafe
//
// OPERAND ORDER IS PART OF THE CONTRACT
// -------------------------------------
// The target is bitwise, so each kernel reproduces the host expression term for
// term rather than an algebraically equal rearrangement. This translation unit
// is in cuda/CMakeLists.txt's `-fmad=false` list for the same reason every
// other qualified kernel is: nvcc would otherwise contract `a*b + c` into an
// FMA and produce a different (more accurate) number than the CPU's rounded
// multiply-then-add.
//
// SCOPE
// -----
// k-epsilon flow-side only, per the frozen P16-GPU-TURB scope. The WALL
// TREATMENT's replacement of production in wall cells is NOT applied here --
// that is a separate traced operation (WallTreatment) and a later step. A
// caller that needs it must apply it after `computeTurbulentProductionDevice`,
// exactly as KEpsilonModel does on the host.

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"
#include "cfd/gpu/DeviceScalarTransport.hpp"
#include "cfd/gpu/TurbulenceBackendTrace.hpp"

namespace cfd::gpu {

// S^2 = 2*(du/dx)^2 + 2*(dv/dy)^2 + (du/dy + dv/dx)^2, per cell.
//
// The device port of cfd::turbulence::computeStrainRateMagnitudeSquared. 2D
// only, matching the host function and the `requireTwoDimensional` guard the
// k-epsilon and k-omega models already carry: gradW is not read.
//
// Records TurbulenceOperation::StrainRate.
//
// THE FALLBACK CONTRACT, shared by all three functions here. When the test-only
// injector forces an operation onto the host, the function records Cpu and
// RETURNS WITHOUT WRITING ITS OUTPUT. The CALLER owns the host path.
//
// That is deliberate, and it matches what the production integration will do:
// the k-epsilon step chooses device or host for the whole operation, it does
// not expect a primitive to quietly substitute one for the other behind its
// back. Leaving the output UNSIZED rather than zero-filling it is the same
// choice made twice -- a downstream call then fails loudly on a length
// mismatch instead of consuming a plausible-looking buffer of zeros and
// producing a wrong answer that still looks like turbulence.
void computeStrainRateMagnitudeSquaredDevice(const DeviceVelocityGradient& gradient,
                                             cfd::Index cellCount,
                                             DeviceBuffer<cfd::Real>& strainRateSquared);

// P_k = mu_t * S^2, per cell. One multiply, exactly as on the host --
// `computeTurbulentProduction` is `turbulentViscosity[i] * s2[i]`.
//
// NOTE the operand order: mu_t first. Records TurbulenceOperation::Production.
void computeTurbulentProductionDevice(const DeviceBuffer<cfd::Real>& turbulentViscosity,
                                      const DeviceBuffer<cfd::Real>& strainRateSquared,
                                      DeviceBuffer<cfd::Real>& production);

// mu_t = density * cMu * kSafe * kSafe / epsSafe, with
// kSafe = max(k, kFloor) and epsSafe = max(epsilon, epsilonFloor).
//
// The floors are NOT a tolerance and NOT clamping of the answer: they exist
// because k and epsilon appear in a denominator, and a cell that has relaxed to
// zero would otherwise produce inf or NaN. They are applied to the INPUTS, in
// the same place and the same order as the host function, so a floored cell
// gets the same number on both backends rather than merely a finite one.
//
// Records TurbulenceOperation::TurbulentViscosity.
void computeTurbulentViscosityDevice(cfd::Real density, cfd::Real cMu,
                                     const DeviceBuffer<cfd::Real>& k,
                                     const DeviceBuffer<cfd::Real>& epsilon, cfd::Real kFloor,
                                     cfd::Real epsilonFloor,
                                     DeviceBuffer<cfd::Real>& turbulentViscosity);

// ---------------------------------------------------------------------------
// P16-GPU-TURB Stage 4 -- the k-equation inputs.
//
// The AUDIT of KEpsilonModel::correct found the k equation needs only three
// things that did not already exist on device. Everything else is reused:
//
//   diffusion / convection   assembleScalarDiffusionDevice / ...ConvectionDevice  (P14)
//   Patankar Su/Sp           applyScalarSourceDevice                              (P14)
//   under-relaxation         applyImplicitUnderRelaxationDevice                   (P16)
//   imposed rows             fixImposedRowsDevice        -- NOT USED BY k, see below
//
// So no second turbulence-specific assembler is built here.
// ---------------------------------------------------------------------------

// Gamma = mu + mu_t/sigma, the device port of
// cfd::turbulence::computeEffectiveDiffusivity.
//
// It is `mu + (mu_t/sigma)` and NOT `(mu + mu_t)/sigma`. Those are different
// closures, not different spellings: the molecular viscosity is not divided by
// the turbulent Prandtl-Schmidt number. The host header calls this out for the
// same reason.
//
// FAIL-CLOSED like the host. The host rejects a non-finite or negative mu_t
// entry; this checks the same condition on the device and throws, reporting one
// flag (4 bytes) rather than downloading the field -- the pattern
// assembleScalarTransportDevice already uses for its sparsity guard. A silently
// negative diffusivity would produce an anti-diffusive matrix that still solves.
//
// Records no TurbulenceOperation: this is an input to the k assembly, not one
// of the eight traced operations.
void computeEffectiveDiffusivityDevice(cfd::Real molecularViscosity,
                                       const DeviceBuffer<cfd::Real>& turbulentViscosity,
                                       cfd::Real sigma, DeviceBuffer<cfd::Real>& diffusivity);

// mu_eff = mu + mu_t, one value per cell -- the device port of the
// deliberately non-virtual TurbulenceModel::effectiveViscosity(mu).
//
// WHY IT IS NOT computeEffectiveDiffusivityDevice(mu, mu_t, 1.0, out). That
// computes mu + mu_t/sigma, a turbulence CLOSURE parameterised by a
// Prandtl-Schmidt number. It coincides with this at sigma = 1 by arithmetic
// accident, not by meaning. The host deliberately keeps effectiveViscosity()
// and computeEffectiveDiffusivity() apart, and sharing one device routine would
// let a later change to the closure silently change the momentum viscosity.
//
// FAIL-CLOSED, like the host. validateTurbulentViscosityField() walks every
// cell rejecting non-finite or negative mu_t; this checks the same condition on
// the device and reports one 4-byte flag rather than downloading the field.
// That is what lets the resident path drop the mu_t download entirely instead
// of keeping it alive just to validate it.
void computeEffectiveViscosityDevice(cfd::Real molecularViscosity,
                                     const DeviceBuffer<cfd::Real>& turbulentViscosity,
                                     DeviceBuffer<cfd::Real>& effectiveViscosity);

// The k equation's Patankar source pair, exactly as KEpsilonModel builds it:
//
//   Su[i] = production[i]
//   Sp[i] = -rho * epsilon[i] / max(k[i], kFloor)
//
// `production` is taken AS GIVEN. When wall functions are active the host has
// already REPLACED the wall cells' entries with the equilibrium expression
// before this point, and the device caller must do the same -- that replacement
// is the WallTreatment operation, not this one. Passing the raw mu_t*S^2 here
// would silently drop the wall treatment while still producing a plausible
// source field.
//
// The kFloor guards a DENOMINATOR. It is not a limiter on Sp and not a
// positivity clip: `Sp*k_new -> -rho*epsilon_old` at self-consistency, and the
// floor only stops that ratio blowing up in a cell whose k has collapsed.
//
// Operand order is the host's: `-rho * epsilon[i] / kSafe` groups as
// `((-rho) * epsilon[i]) / kSafe`.
void computeKSourceTermsDevice(cfd::Real density, const DeviceBuffer<cfd::Real>& k,
                               const DeviceBuffer<cfd::Real>& epsilon,
                               const DeviceBuffer<cfd::Real>& production, cfd::Real kFloor,
                               DeviceBuffer<cfd::Real>& Su, DeviceBuffer<cfd::Real>& Sp);

// solved[i] = max(solved[i], floorValue), the post-solve floor
// solveRelaxedScalarTransport applies.
//
// This is the LAST step of the host order and is easy to forget, because the
// system is already correct without it. It is not cosmetic: the floor is what
// keeps a cell that undershot from feeding a negative k into the next
// iteration mu_t and Sp.
//
// Imposed values are NOT re-asserted here -- k has no imposed cells (only
// epsilon does), so there is nothing to re-assert. The epsilon path will need
// the re-assertion that follows this floor on the host.
void applyScalarFloorDevice(cfd::Real floorValue, DeviceBuffer<cfd::Real>& field);

// The epsilon equation's Patankar source pair, the device port of
// cfd::turbulence::computeEpsilonSourceTerms:
//
//   epsOverK = epsilon[i] / max(k[i], kFloor)
//   Su[i]    = c1Epsilon * epsOverK * production[i]
//   Sp[i]    = -c2Epsilon * density * epsOverK
//
// epsOverK is formed ONCE and reused, as on the host. Folding the division
// into Su as `c1*epsilon*production/kSafe` reassociates it past two
// multiplications and is a different double.
//
// `production` is again taken AS GIVEN, with the wall cells already replaced
// where wall functions are active -- so the wall treatment reaches epsilon
// through BOTH channels: this source, and the imposed rows below.
void computeEpsilonSourceTermsDevice(cfd::Real density, cfd::Real c1Epsilon, cfd::Real c2Epsilon,
                                     const DeviceBuffer<cfd::Real>& k,
                                     const DeviceBuffer<cfd::Real>& epsilon,
                                     const DeviceBuffer<cfd::Real>& production, cfd::Real kFloor,
                                     DeviceBuffer<cfd::Real>& Su, DeviceBuffer<cfd::Real>& Sp);

// The post-solve re-assertion: field[cell] = value, for every imposed cell.
//
// ORDER IS LOAD-BEARING AND IS NOT OBVIOUS. The host does
//
//     solve  ->  floor  ->  re-assert imposed
//
// in that order, and the reason is written in solveRelaxedScalarTransport: "An
// imposed value is a boundary condition, not a solver output, so the floor must
// not move it." A wall epsilon BELOW the floor is legal, and reversing these
// two steps silently replaces it with the floor -- the cell then stops
// satisfying the wall function that put it there, while every field stays
// finite and plausible.
//
// Reversing the order is undetectable unless a test supplies an imposed value
// below the floor. `test_device_epsilon_transport.cpp` does exactly that.
void reassertImposedValuesDevice(const DeviceImposedCells& imposed, DeviceBuffer<cfd::Real>& field);

// The same operation against buffers that are ALREADY device resident.
//
// WHY THIS OVERLOAD EXISTS. applyWallProductionDevice used to DOWNLOAD
// `wall.wallCells` and `wall.wallCellProduction`, hand the host copies straight
// back to uploadImposedCellsDevice, and re-upload them as a fresh
// DeviceImposedCells -- a pure device -> host -> device round trip of
// N_wallcells * 16 bytes each way, for data no host code reads. The two buffers
// it downloaded ARE the two buffers this kernel needs. Measured before removal:
// 2048 B down and 2048 B up per correction at 128 wall cells
// (results/p16/turbulence/gpu_equivalence/wall_transfer/).
//
// The DeviceImposedCells overload forwards here, so there is ONE kernel launch
// path and no duplicated code.
//
// VALIDATION, STATED PLAINLY. uploadImposedCellsDevice checks that every index
// is in range, that every value is finite, and that no cell repeats. This
// overload cannot: its inputs are already on the device. Callers must therefore
// pass a list that has been validated elsewhere. That is not a relaxation for
// the one caller this exists for -- applyWallProductionDevice's list is
// `wall.wallCells`, the SAME list uploadImposedCellsDevice already validates
// against the same cell count when `wall.imposedEpsilon` is built
// (GpuKEpsilonCorrectionCuda.cpp), and its uniqueness is guaranteed upstream by
// computeWallTreatment, which keeps one slot per cell.
void reassertImposedValuesDevice(const DeviceBuffer<cfd::Index>& cells,
                                 const DeviceBuffer<cfd::Real>& values,
                                 DeviceBuffer<cfd::Real>& field);

// P16-GPU-TURB Stage 7I step 3 -- compact gather.
//
// out[slot] = source[indices[slot]], one thread per SLOT.
//
// This is what keeps the host wall treatment fed without downloading a field.
// The audited wall law reads only `velocity[owner]` and `k[owner]` for
// wall-owner cells, so the transfer that has to cross PCIe is O(wall cells),
// not O(cells). `indices` is the wall-owner list in WALL-FACE ORDER, which the
// sequential corner rule depends on and which this must not disturb --
// gathering is a permutation, not a sort.
void gatherByIndexDevice(const DeviceBuffer<cfd::Index>& indices,
                         const DeviceBuffer<cfd::Real>& source, DeviceBuffer<cfd::Real>& out);

}  // namespace cfd::gpu
