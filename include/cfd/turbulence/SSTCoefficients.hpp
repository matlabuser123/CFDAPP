#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::turbulence {

// Menter's original (1994) SST k-omega coefficient set -- P2-TURB-006
// section 5. This is the "standard"/original SST formulation (not the
// 2003 BSL-SST revision, and not a transition/rough-wall/compressible
// variant -- section 46/59's own "explicitly distinguish standard
// k-omega from SST k-omega" instruction, extended here to distinguish
// exactly which SST too).
//
// Two coefficient sets are blended (SSTEquation.hpp's blend()) via F1:
// set 1 is the *inner* (near-wall) set, algebraically identical in
// shape to standard k-omega (P2-TURB-005's own KOmegaCoefficients, but
// NOT the same values -- sigmaK1/sigmaOmega1/beta1 below differ from
// KOmegaCoefficients' sigmaK/sigmaOmega/beta on purpose, per Menter's
// own SST derivation, so this is deliberately its own type, not a reuse
// of KOmegaCoefficients); set 2 is the *outer* (far-field) set, derived
// to recover k-epsilon-like behavior away from walls.
//
// a1 is the eddy-viscosity limiter's own constant (SSTEquation.hpp's
// computeSSTTurbulentViscosity). kappa is the von Karman constant, used
// only to derive alpha1/alpha2 (SSTEquation.hpp's computeSSTAlpha) via
// the standard SST log-law consistency relation -- not an independent
// tunable of this implementation.
struct SSTCoefficients {
  Real betaStar{0.09};
  Real a1{0.31};
  Real kappa{0.41};

  // Inner (near-wall) set -- F1 -> 1 here.
  Real sigmaK1{0.85};
  Real sigmaOmega1{0.5};
  Real beta1{0.075};

  // Outer (far-field) set -- F1 -> 0 here.
  Real sigmaK2{1.0};
  Real sigmaOmega2{0.856};
  Real beta2{0.0828};

  // Production limiter constant (P2-TURB-006 section 19):
  //   P_k_limited = min(P_k, productionLimiterFactor * betaStar * rho * k * omega)
  // 10.0 is Menter's own originally-published SST production limiter
  // factor, the value used throughout the public SST literature and
  // most production CFD codes (e.g. OpenFOAM's kOmegaSST) -- not a
  // guessed value (section 19's own "do not guess the constant").
  Real productionLimiterFactor{10.0};
};

}  // namespace cfd::turbulence
