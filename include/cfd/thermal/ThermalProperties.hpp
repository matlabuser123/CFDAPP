#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::thermal {

// Constant thermal properties for the initial thermal-transport model
// (P2 -- Thermal, P2-THERMAL-001): thermal conductivity and specific heat
// are fixed numbers, not fields -- no k(T), cp(T), or phase-dependent
// behavior yet (see TODO.md). Same design as
// cfd::physics::FluidProperties: a thin, validated value type where
// construction is the only place invalid physics (non-positive or
// non-finite conductivity/specific heat) can enter, so every downstream
// consumer can assume a valid ThermalProperties without re-checking.
//
// Density is deliberately *not* stored here. cfd::physics::FluidProperties
// already owns density as this project's single source of truth for it
// (P0 -- Incompressible Physics); duplicating a density field on
// ThermalProperties would let a case supply two different density values
// to the same simulation with no way to detect the disagreement. Instead,
// thermalDiffusivity() below takes density as an explicit parameter --
// callers (e.g. the energy equation) pass the same FluidProperties::
// density() they already use for the flow solve, keeping density
// single-sourced.
class ThermalProperties {
 public:
  // Throws InvalidArgumentError if conductivity or specificHeat is not
  // finite and strictly positive. Zero conductivity (perfectly insulating
  // material) or zero specific heat are outside this model's supported
  // scope -- not silently accepted and clamped to a tiny epsilon.
  ThermalProperties(Real conductivity, Real specificHeat);

  [[nodiscard]] Real conductivity() const noexcept;
  [[nodiscard]] Real specificHeat() const noexcept;

  // alpha = k / (rho * cp).
  //
  // Throws InvalidArgumentError if density is not finite and strictly
  // positive -- the same validation FluidProperties itself already
  // applies to density, repeated here because this function accepts
  // density as a bare Real rather than a FluidProperties reference (so
  // ThermalProperties has no compile-time dependency on cfd::physics).
  [[nodiscard]] Real thermalDiffusivity(Real density) const;

 private:
  Real conductivity_{};
  Real specificHeat_{};
};

}  // namespace cfd::thermal
