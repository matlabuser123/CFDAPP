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
#include <string>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::gpu {

// Kept as plain constants rather than an enum so the value can live in a
// DeviceBuffer<Index> and be compared in device code without a cast.
inline constexpr cfd::Index kBoundaryEncodingConstant = 0;
inline constexpr cfd::Index kBoundaryEncodingShift = 1;
inline constexpr cfd::Index kBoundaryEncodingAffine = 2;

// Why `encodeBoundaryCondition` below refused `bc`, phrased for the plan that
// asked. `pathName` is that plan's own word ("diffusion", "gradient",
// "convection"), or nullptr for a plan that does not name one.
//
// One place rather than one per plan. A device plan calls the encoder and, on
// false, reports this -- it does not need to know which conditions have a more
// specific cause than "not representable", and cannot drift from the others
// when one is added. The P15 gate found exactly that drift: two plans had been
// taught to name the convective wall's real cause and a third had not, and the
// third ran first, so its generic message was the only one anyone ever saw.
[[nodiscard]] inline std::string unsupportedEncodingReason(
    cfd::Index faceIndex, const cfd::boundary::BoundaryCondition& bc, const char* noun,
    const char* pathName) {
  const std::string face =
      "face " + std::to_string(faceIndex) + ": " + noun + " '" + std::string(bc.name()) + "'";

  // The convective wall is NOT "not representable" -- it is affine in T_P, and
  // the encoding below could hold it. What the plan cannot obtain is the
  // conductivity in G = 1/(d/k + 1/h_c): it varies per cell and with
  // temperature, while a plan is built once from mesh and boundaries alone.
  // Saying so is the difference between a user knowing to run this case on the
  // CPU and a user believing their boundary condition is malformed.
  if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {
    return face +
           " requires the runtime thermal conductivity to form its coefficients and is not "
           "supported by the current device " +
           (pathName != nullptr ? std::string(pathName) + " plan" : std::string("boundary plan")) +
           "; the CPU thermal assembly handles it";
  }

  return face +
         " is not bitwise-reproducible as a constant, a shift or an affine function of phi_P" +
         (pathName != nullptr ? "; mesh rejected for the device " + std::string(pathName) + " path"
                              : std::string());
}

inline bool encodeBoundaryCondition(const cfd::boundary::ScalarBoundaryCondition& bc,
                                    cfd::Real distance, cfd::Real& a, cfd::Real& b,
                                    cfd::Index& kind) {
  using cfd::Real;

  // P15: refuse the convective (Robin) wall BEFORE the probe loop below.
  //
  // ConvectiveHeatTransfer::boundaryValue() throws by design -- the face value
  // depends on the local conductivity, which this interface cannot supply, and
  // returning a plausible wrong number would be consumed silently by every
  // assembler. Probing it here would propagate that exception out of GPU plan
  // construction instead of taking the graceful "not GPU-supported" path every
  // other unsupported condition takes.
  //
  // Note what is NOT the reason: the condition is perfectly affine in T_P, so
  // the encoding below could represent it. What it cannot obtain is the
  // conductivity, which varies per cell and (after P15) with temperature,
  // while the plan is built once from mesh and boundaries alone. See
  // results/p15/thermal-energy/robin_architecture_decision.md.
  if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {
    return false;
  }
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
