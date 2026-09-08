#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::physics {

// Constant fluid properties for the initial incompressible, Newtonian,
// laminar model (P0 -- Incompressible Physics): density and dynamic
// viscosity are fixed numbers, not fields -- no rho(x,y), mu(T),
// compressibility, or non-Newtonian behavior yet (see TODO.md). A thin,
// validated value type: construction is the only place invalid physics
// (non-positive or non-finite density/viscosity) can enter, so every
// downstream consumer can assume a valid FluidProperties without
// re-checking.
class FluidProperties {
 public:
  // Throws InvalidArgumentError if density or dynamicViscosity is not
  // finite and strictly positive. Zero viscosity (inviscid) is outside
  // this model's supported Newtonian-viscous scope -- it is not silently
  // accepted and clamped to a tiny epsilon.
  FluidProperties(Real density, Real dynamicViscosity);

  [[nodiscard]] Real density() const noexcept;
  [[nodiscard]] Real dynamicViscosity() const noexcept;

  // nu = mu / rho.
  [[nodiscard]] Real kinematicViscosity() const noexcept;

 private:
  Real density_{};
  Real dynamicViscosity_{};
};

}  // namespace cfd::physics
