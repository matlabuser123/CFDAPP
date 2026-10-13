#pragma once

#include <cmath>

#include "cfd/core/Types.hpp"

// P16 -- the HIGHER-Re turbulent channel case, Re_tau = 590.
//
// WHY A SECOND CASE EXISTS
// ------------------------
// Re_tau = 180 (ChannelReTau180.hpp) stays as the low-Re stress case, and its
// sweep is preserved. But it cannot be the primary wall-function accuracy
// benchmark, and the reason is structural rather than a modelling failure:
//
//   y_1^+ = Re_tau * y_1 / delta,  and a uniform mesh has y_1 = delta/ny
//   =>  y_1^+ = Re_tau / ny
//
// So the number of wall-normal cells a high-Re wall function permits is
//
//   ny <= Re_tau / 30
//
// At Re_tau = 180 that is ny <= 6 -- three cells per half-channel, which is too
// coarse to say anything meaningful about the core. The measured sweep shows
// exactly that tension: accuracy improved monotonically with refinement while
// validity was lost monotonically (results/p16/turbulence/acceptance/
// threshold_derivation.md).
//
// At Re_tau = 590 the same inequality gives ny <= 19, so a mesh can satisfy the
// validity band at EVERY wall face and still carry ~8-9 cells per half-channel.
// Both requirements are simultaneously satisfiable, which is what makes this the
// primary accuracy benchmark.
//
// SOURCE
// ------
// Moser, R. D., Kim, J., & Mansour, N. N. (1999). Direct numerical simulation of
// turbulent channel flow up to Re_tau = 590. Physics of Fluids 11(4), 943-945.
//
// That is the SAME paper ChannelReTau180.hpp already cross-checks its Re_tau =
// 180 values against -- this repository already treats it as an accepted
// reference, which is why Re_tau = 590 was chosen rather than a higher case
// from a dataset the repository has never used. It is the largest Re_tau in
// that dataset, i.e. the most wall-normal room available without introducing a
// new reference.
//
// The Reynolds number was NOT chosen to make the solver pass. It was chosen by
// the ny <= Re_tau/30 inequality above, before any Re_tau = 590 run existed.
namespace cfd::validation::channel_re_tau_590 {

inline constexpr cfd::Real kReTau = 590.0;

// The bulk Reynolds number that produces Re_tau = 590.
//
// DERIVED, not quoted, and the derivation is checkable. Dean's (1978)
// correlation for plane channel flow,
//
//   Re_tau = 0.09 * Re_bulk^0.88,     Re_bulk = U_bulk * (2 delta) / nu
//
// inverted at Re_tau = 590 gives Re_bulk = (590/0.09)^(1/0.88) = 21725.
//
// Cross-checked against this repository's own Re_tau = 180 pairing: the same
// inversion gives Re_bulk = 5647 where ChannelReTau180.hpp uses 5600 -- 0.8%.
// So the correlation reproduces the value this project already relies on, which
// is the evidence that it is being applied correctly here.
//
// A correlation is not a measurement, and this one carries a few percent of
// uncertainty. That matters because G2 measures |Re_tau,CFD - 590|/590, so an
// error in Re_bulk biases G2 directly. It is stated rather than hidden, and the
// cross-check above is what bounds it.
inline constexpr cfd::Real kReBulk = 21725.0;

// Standard smooth-wall log-law constants, identical to the Re_tau = 180 file's.
// Universal by construction -- that is the point of wall units.
inline constexpr cfd::Real kKappa = 0.41;
inline constexpr cfd::Real kB = 5.0;

// The largest uniform ny whose first cell still reaches y+ = 30 at this Re_tau.
// Recorded as a constant so the mesh family below cannot silently drift past
// the limit that motivated this case.
inline constexpr cfd::Index kMaximumWallFunctionNy = 19;

}  // namespace cfd::validation::channel_re_tau_590
