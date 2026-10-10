#pragma once

// P16 -- the NASA Turbulence Modeling Resource 2D Backward Facing Step, i.e.
// Driver, D. M. & Seegmiller, H. L. (1985), "Features of a reattaching
// turbulent shear layer in divergent channel flow", AIAA Journal 23(2),
// 163-171.
//
// WHY A SECOND STEP CASE
// ----------------------
// BackwardFacingStepCase.hpp is Gartling's LAMINAR step: expansion ratio 2,
// Re = 800, a parabolic inlet. It cannot be made into this one -- the geometry
// (ER 1.125), the Reynolds number (36,000) and the inlet (a developed turbulent
// boundary layer) all differ -- so this is its own generator rather than a
// parameterisation that would leave neither case clearly defined.
//
// The internal Re_h = 5000 step stays too, as the qualitative
// mechanism-discrimination benchmark it was built to be. This one is the
// QUANTITATIVE case, gated against a named reference.
//
// REFERENCE VALUES
// ----------------
//   expansion ratio   1.125  (inlet 8H, outlet 9H, H = step height)
//   Re_H              36,000 (U_ref H / nu)
//   reattachment      x_r/H = 6.26
//
// WHAT IS DELIBERATELY NOT CLAIMED
// --------------------------------
// The tabulated C_f(x), C_p(x) and velocity-profile data are NOT available in
// this environment. They are exported as evidence and checked for the
// structure a separated flow must have; they are NOT compared against
// remembered numbers. See
// results/p16/turbulence/acceptance/separated_flow_methodology.md section 2 --
// that document was frozen before this file existed.
//
// DEVIATIONS, declared in that same frozen document before any run:
//   * no upstream development section -- the domain starts at the step, so the
//     inlet boundary layer is PRESCRIBED (1/7 power law, delta/H ~ 1.5) rather
//     than developed. This is the most significant deviation.
//   * incompressible; straight top wall; uniform Cartesian mesh; 2-D.

#include <string>
#include <utility>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::validation::driver_seegmiller {

// Step height, the length scale everything is reported in.
inline constexpr Real kStepHeight = 1.0;
// ER = 1.125: the channel is 9H tall downstream of the step, 8H upstream.
inline constexpr Real kChannelHeight = 9.0 * kStepHeight;
inline constexpr Real kExpansionRatio = 9.0 / 8.0;
// Streamwise extent downstream of the step, in step heights. Long enough that
// the outlet sits well past reattachment (x_r/H = 6.26) and the recovery
// region is resolved.
inline constexpr Real kDomainLength = 30.0 * kStepHeight;

inline constexpr Real kReferenceVelocity = 1.0;
inline constexpr Real kDensity = 1.0;
inline constexpr Real kReynoldsH = 36000.0;
inline constexpr Real kDynamicViscosity = kDensity * kReferenceVelocity * kStepHeight / kReynoldsH;

// The experimental reattachment length. The single quantity this case gates on
// -- see the frozen methodology for why it is the only one.
inline constexpr Real kReferenceReattachment = 6.26;

// Inlet boundary-layer thickness at the step, from the reference.
inline constexpr Real kInletBoundaryLayerThickness = 1.5 * kStepHeight;

struct MeshSpec {
  const char* label;
  // Cells across ONE step height. The full channel is 9x this, so the cell
  // count grows quickly -- which is why the sequence is coarse/medium/fine
  // rather than a long refinement ladder.
  Index cellsPerStepHeight;
};

// Cell counts implied by a spec, exposed so the evidence can record them
// without reconstructing the mesh.
[[nodiscard]] Index cellsWallNormal(const MeshSpec& spec);
[[nodiscard]] Index cellsStreamwise(const MeshSpec& spec);

// The domain downstream of the step: [0, L] x [0, 9H], with the left boundary
// split into
//   "step"         -- y in [0, H), the vertical step face, no-slip;
//   "inlet_<face>" -- y in [H, 9H), one patch per face carrying the prescribed
//                     inlet profile.
// Same split technique BackwardFacingStepCase already uses; no new mesh type.
//
// Throws std::invalid_argument if cellsPerStepHeight is 0.
[[nodiscard]] mesh::Mesh makeMesh(const MeshSpec& spec);

// u(y) at the inlet: a 1/7-power-law turbulent boundary layer of thickness
// delta growing from the step lip, capped at the free-stream value.
//
//   y' = y - H                      distance above the step lip
//   u  = U_ref * (y'/delta)^(1/7)   for y' < delta
//   u  = U_ref                      above it
//
// The face AVERAGE is used, not the centre value, so the inflow rate is
// mesh-independent -- the same convention the laminar harness uses for its
// parabola, and for the same reason.
[[nodiscard]] Real averagedInletVelocity(Real y0, Real y1);

struct Boundaries {
  boundary::BoundaryConditionSet velocity;
  boundary::BoundaryConditionSet pressure;
};
// Velocity: inlet patches Inlet(face-average, 0); "step", "bottom", "top" Wall;
// "right" Outlet. Pressure: zero gradient except FixedValue(0) at the outlet.
[[nodiscard]] Boundaries makeBoundaries(const mesh::Mesh& mesh);

// Bottom-wall skin friction and pressure coefficient along the wall.
//
//   C_f = tau_w / (0.5 rho U_ref^2)     SIGNED -- negative inside the bubble,
//                                       which is what reattachment is read from
//   C_p = (p - p_ref) / (0.5 rho U_ref^2)
//
// p_ref is the bottom-wall pressure at the OUTLET, so C_p is referenced to a
// recovered station rather than to a point inside the bubble.
//
// tau_w is the SIGNED wall shear. For the wall-function runs the magnitude
// comes from the model's own wall shear where it is available, and the SIGN
// from the wall-adjacent tangential velocity -- the model's tau_w is a
// magnitude and carries no direction, and a reattachment detector needs the
// sign. Where no model wall shear is supplied the laminar secant is used, and
// which one was used is recorded.
struct WallDistribution {
  std::vector<Real> x;  // x/H from the step
  std::vector<Real> cf;
  std::vector<Real> cp;
};
[[nodiscard]] WallDistribution bottomWallDistribution(const mesh::Mesh& mesh, const MeshSpec& spec,
                                                      const fields::VectorField& velocity,
                                                      const fields::ScalarField& pressure,
                                                      Real dynamicViscosity,
                                                      const fields::ScalarField* modelWallShear);

// The outlet velocity profile, u/U_ref against y/H.
[[nodiscard]] std::vector<std::pair<Real, Real>> outletProfile(const mesh::Mesh& mesh,
                                                               const MeshSpec& spec,
                                                               const fields::VectorField& velocity);

// --- the precursor inflow (P16, Approach A) ----------------------------------
//
// The reference inflow profiles are not obtainable in this environment (see
// separated_flow/reference_specification.md section 5), so the incoming
// boundary layer is DEVELOPED by CFDApp in a separate upstream duct and mapped
// into the step inlet.
//
// CRITICAL, and the thing the first attempt got wrong: the reference layer is
// NOT fully developed channel flow. delta/H is about 1.5 in an 8H duct, where
// fully developed would be delta = 4H. So the precursor is run long and
// SAMPLED at the station where delta/H first reaches 1.5 -- it is not run to
// equilibrium. Running it to equilibrium would produce a confidently wrong
// inlet.

struct PrecursorProfile {
  // Wall-normal coordinate measured from the duct's bottom wall, which becomes
  // the STEP LIP: precursor y maps to step-case y + H.
  std::vector<Real> y;
  std::vector<Real> u;
  std::vector<Real> v;
  std::vector<Real> k;
  std::vector<Real> omega;

  // Verification quantities, checked BEFORE any separation result is looked at.
  Real extractionStationOverH{};  // x/H where the profile was taken
  Real deltaOverH{};              // 99% boundary-layer thickness
  Real edgeVelocity{};
  Real skinFriction{};  // C_f at the extraction station
  Real massFlowPerDepth{};
  Real maximumTurbulentViscosityRatio{};
  bool converged{};
  Index iterations{};
};

// Duct height for the precursor: 8H, the reference upstream channel.
inline constexpr Real kUpstreamChannelHeight = 8.0 * kStepHeight;
// The target the precursor is sampled at -- the reference value, fixed here
// rather than discovered from the run.
inline constexpr Real kTargetDeltaOverH = 1.5;

struct PrecursorSpec {
  Index cellsWallNormal{80};
  // Long enough that delta/H = 1.5 is reached inside the domain. A flat-plate
  // estimate (delta/x = 0.37 Re_x^-1/5) puts it near x/H = 79, so 150 gives
  // margin without relying on that estimate being right -- the station is
  // FOUND, not assumed.
  Real lengthOverH{150.0};
  Index cellsStreamwise{150};
};

// Solves the upstream duct and returns the profile at the station where the
// bottom-wall boundary layer first reaches kTargetDeltaOverH.
//
// Throws std::runtime_error if that thickness is never reached inside the
// domain -- silently returning the outlet profile would substitute a different
// inflow state for the one asked for.
[[nodiscard]] PrecursorProfile runPrecursor(const PrecursorSpec& spec);

// Linear interpolation of a precursor profile at a step-case wall-normal
// coordinate. `yStepCase` is measured in the step domain, so the duct wall sits
// at y = H.
[[nodiscard]] Real sampleProfile(const std::vector<Real>& y, const std::vector<Real>& value,
                                 Real yStepCase);

// Boundaries whose inlet carries the mapped precursor profile instead of the
// analytic 1/7 power law. Passing nullptr reproduces makeBoundaries exactly,
// which is what makes the A/B comparison a controlled one.
[[nodiscard]] Boundaries makeBoundariesWithProfile(const mesh::Mesh& mesh,
                                                   const PrecursorProfile* profile);

// The k and omega inlet values per face, mapped from the precursor. Empty when
// no profile is given.
struct ScalarInletProfiles {
  std::vector<std::pair<std::string, Real>> k;
  std::vector<std::pair<std::string, Real>> omega;
};
[[nodiscard]] ScalarInletProfiles mapScalarInlets(const mesh::Mesh& mesh,
                                                  const PrecursorProfile& profile);

}  // namespace cfd::validation::driver_seegmiller
