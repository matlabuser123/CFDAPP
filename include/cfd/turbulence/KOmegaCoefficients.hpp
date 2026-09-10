#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::turbulence {

// The classical standard (Wilcox) k-omega model constants -- P2-TURB-005.
// Deliberately a *different* named type from KEpsilonCoefficients, not a
// reinterpretation of it: standard k-omega, BSL k-omega, and SST k-omega
// each define their own distinct constant sets (TODO.md P2-TURB-005
// section 3 -- "do not mix constants from standard k-omega, BSL k-omega,
// SST"), and SST's own constants (P2-TURB-006, not this task) belong in
// their own type when that task arrives, not folded in here as optional
// fields.
//
// Centralized here, the same reasoning as KEpsilonCoefficients: every
// formula that uses one of these names it, instead of repeating a bare
// literal through KOmegaModel/KOmegaEquation.
struct KOmegaCoefficients {
  Real betaStar{0.09};
  Real alpha{5.0 / 9.0};
  Real beta{0.075};
  Real sigmaK{2.0};
  Real sigmaOmega{2.0};
};

}  // namespace cfd::turbulence
