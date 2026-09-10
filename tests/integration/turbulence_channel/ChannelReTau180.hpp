#pragma once

#include "cfd/core/Types.hpp"

// Kim, J., Moin, P., & Moser, R. (1987). Turbulence statistics in fully
// developed channel flow at low Reynolds number. Journal of Fluid
// Mechanics, 177, 133-166. Cross-checked against Moser, Kim & Mansour
// (1999), Physics of Fluids 11(4), 943-945 (same Re_tau=180 case).
//
// These are the single source of truth for the P2-TURB-007 turbulent
// channel benchmark's literature comparison, mirroring
// GhiaRe100.hpp's own "hardcoded constexpr is the source of truth,
// validation/ carries a human-readable documented copy" convention --
// see validation/data/turbulence/channel_flow/README.md and
// reference_summary.json (must be kept numerically in sync with this
// file; that README also documents why this is a small set of
// well-established summary statistics plus the universal law-of-the-wall
// relations, rather than a fine-grained point-by-point DNS table this
// sandbox could not fetch from the live database mirrors).
namespace cfd::validation::channel_re_tau_180 {

// Re_tau = u_tau * delta / nu, delta = channel half-height.
inline constexpr cfd::Real kReTau = 180.0;
// Re_bulk = U_bulk * H / nu, H = 2*delta (full channel height) -- the
// quantity CFDApp's own case setup controls directly via the inlet
// velocity/domain size.
inline constexpr cfd::Real kReBulk = 5600.0;
// Re_centerline = U_centerline * delta / nu.
inline constexpr cfd::Real kReCenterline = 3300.0;
inline constexpr cfd::Real kUCenterlinePlus = 18.2;
inline constexpr cfd::Real kUBulkPlus = 15.6;
// Cf = 2*(u_tau/U_bulk)^2, derived from kUBulkPlus above.
inline constexpr cfd::Real kSkinFrictionCoefficient = 8.2e-3;

// Standard smooth-wall law-of-the-wall constants (section 15/41):
// viscous sublayer u+ = y+ (y+ < kViscousSublayerMaxYPlus), log layer
// u+ = (1/kappa)*ln(y+) + B (y+ > kBufferLayerMaxYPlus); the interval
// between is the buffer layer, where neither closed form applies.
inline constexpr cfd::Real kKappa = 0.41;
inline constexpr cfd::Real kB = 5.0;
inline constexpr cfd::Real kViscousSublayerMaxYPlus = 5.0;
inline constexpr cfd::Real kBufferLayerMaxYPlus = 30.0;

}  // namespace cfd::validation::channel_re_tau_180
