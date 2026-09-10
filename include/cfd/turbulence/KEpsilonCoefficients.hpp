#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::turbulence {

// The classical standard k-epsilon model constants (Launder & Spalding
// 1974) -- P2-TURB-004. Centralized here, as a single named value type,
// rather than scattered as magic numbers through KEpsilonModel/
// KEpsilonEquation, so every formula that uses one of these can name it
// instead of repeating a bare literal (TODO.md P2-TURB-004 section 3).
//
// Deliberately just data (an aggregate with defaulted values, mirroring
// cfd::pressure_velocity::SIMPLESettings/PISOSettings's own shape) -- no
// behavior, no validation here. KEpsilonModel validates the coefficients
// it is actually given (finite, and each individually meaningful: e.g.
// Cmu > 0) at the one point they enter a real computation, the same
// "validate at the point of use" convention as this codebase's other
// configuration structs.
struct KEpsilonCoefficients {
  Real cMu{0.09};
  Real c1Epsilon{1.44};
  Real c2Epsilon{1.92};
  Real sigmaK{1.0};
  Real sigmaEpsilon{1.3};
};

}  // namespace cfd::turbulence
