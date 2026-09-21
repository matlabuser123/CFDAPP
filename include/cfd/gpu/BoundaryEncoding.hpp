#pragma once

// GPU-DISC-001 -- host-side encoding of a ScalarBoundaryCondition so a kernel
// can evaluate it without a virtual call.
//
// Extracted from DeviceGradientPlan.cpp (GPU-DISC-001B) when the diffusion plan
// (001C) needed the same encoding at the same distance. Moved, not copied: two
// copies of this would drift, and the thing it guarantees -- bitwise
// reproduction of boundaryValue -- is exactly the kind of property a drifted
// copy breaks silently. 001B's 132-case bitwise differential is re-run after
// the move to show its behaviour is unchanged.
//
// The device must reproduce the condition's ARITHMETIC, not merely its value in
// exact arithmetic. Three forms cover every scalar condition in the codebase:
//
//   constant   value = a          FixedValue, FixedTemperature, WallOmega
//   shift      value = phiP + a   FixedGradient, Adiabatic, HeatFlux
//   affine     value = a + b*phiP anything else still affine in phiP
//
// The shift form is not a special case of the affine one. A Neumann condition
// computes `ownerValue + (gradient * normalDistance)`, one rounded product then
// one rounded add, and a = boundaryValue(0, d) recovers that product exactly.
// Recovering b as boundaryValue(1, d) - a does NOT give exactly 1.0:
// fl(1 + a) - a differs from 1 whenever a carries bits below one ULP of (1 + a).
//
// Whichever form is chosen is checked against the condition itself by exact bit
// comparison over a spread of probes. A condition matching none of them makes
// the caller reject the mesh rather than approximate it.

#include <cstring>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::gpu {

// Kept as plain constants rather than an enum so the value can live in a
// DeviceBuffer<Index> and be compared in device code without a cast.
inline constexpr cfd::Index kBoundaryEncodingConstant = 0;
inline constexpr cfd::Index kBoundaryEncodingShift = 1;
inline constexpr cfd::Index kBoundaryEncodingAffine = 2;

inline bool encodeBoundaryCondition(const cfd::boundary::ScalarBoundaryCondition& bc,
                                    cfd::Real distance, cfd::Real& a, cfd::Real& b,
                                    cfd::Index& kind) {
  using cfd::Real;
  const Real probes[] = {0.0, 1.0, -1.0, 0.25, 3.5, -7.125, 1e3, -1e-3, 1e8, -3.7e-7, 273.15};
  const auto reproduces = [&](const auto& evaluate) {
    for (const Real phi : probes) {
      const Real expected = bc.boundaryValue(phi, distance);
      const Real encoded = evaluate(phi);
      if (std::memcmp(&expected, &encoded, sizeof(Real)) != 0) return false;
    }
    return true;
  };

  a = bc.boundaryValue(0.0, distance);
  if (reproduces([&](Real) { return a; })) {
    b = 0.0;
    kind = kBoundaryEncodingConstant;
    return true;
  }
  if (reproduces([&](Real phi) { return phi + a; })) {
    b = 1.0;
    kind = kBoundaryEncodingShift;
    return true;
  }
  b = bc.boundaryValue(1.0, distance) - a;
  if (reproduces([&](Real phi) { return a + (b * phi); })) {
    kind = kBoundaryEncodingAffine;
    return true;
  }
  return false;
}

}  // namespace cfd::gpu
