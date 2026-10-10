#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::compressible {

// P3-PHYS-006: equation-of-state abstraction -- every compressible
// density calculation in this module goes through one of these (this
// task's own section 5: "Do not hard-code ideal-gas calculations
// throughout momentum, continuity, and energy code"). Mirrors the
// "solver consumes a value, does not know how it was produced"
// separation already established for
// cfd::physics::TemperatureProperty (P3-PHYS-003) and
// cfd::boundary::BoundaryCondition.
class EquationOfState {
 public:
  virtual ~EquationOfState() = default;

  // rho(p, T). p is *absolute* thermodynamic pressure (section 23 --
  // never a gauge/reference-relative pressure), T is absolute
  // temperature. Throws InvalidArgumentError if p or T is not finite and
  // strictly positive, or if the resulting density is not finite and
  // strictly positive.
  [[nodiscard]] virtual Real density(Real pressure, Real temperature) const = 0;

  // d(rho)/dp at constant T -- needed by the pressure-density correction
  // relation (section 15/34). Analytical, not a finite difference
  // (section 15's own "do not approximate... using arbitrary finite
  // differences if analytical derivatives are available").
  [[nodiscard]] virtual Real dDensityDPressure(Real pressure, Real temperature) const = 0;

  // d(rho)/dT at constant p.
  [[nodiscard]] virtual Real dDensityDTemperature(Real pressure, Real temperature) const = 0;

  // P17.2: the inverse of density(p, T) -- p(rho, T).
  //
  // The returned pressure is **ABSOLUTE thermodynamic pressure**, the same
  // quantity density() consumes, and never a gauge or reference-relative
  // pressure. The existing convention is unchanged and is repeated here because
  // this is the direction that produces a pressure rather than consuming one,
  // so it is the one a caller could most easily misread:
  //
  //     SIMPLE solves          gauge pressure
  //     this EOS consumes and
  //     produces               absolute pressure
  //     referencePressure      the single documented bridge between them
  //                            (CompressibleSIMPLE.hpp, PhysicsConfig.hpp,
  //                             ProjectRunner's pressureAbsolute)
  //
  // On the abstraction rather than on IdealGasEOS so that generic compressible
  // code -- the energy equation and the Riemann flux to come -- never has to know
  // or down-cast to a concrete EOS type to invert the state relation.
  //
  // Throws InvalidArgumentError if rho or T is not finite and strictly positive,
  // or if the resulting pressure is not finite and strictly positive -- the same
  // contract density() states, in the same direction.
  [[nodiscard]] virtual Real pressure(Real density, Real temperature) const = 0;

  // P17.2: the third direction -- T(p, rho). With density(p,T) and pressure(rho,T) this
  // completes the two-argument closure: any one of {p, rho, T} is recoverable from the
  // other two, so no caller ever has to write the state relation itself.
  //
  // ARGUMENT ORDER. The rule that makes all three signatures consistent is *pressure is
  // the first argument whenever pressure is an input*:
  //
  //     density(pressure, temperature)      p first
  //     pressure(density, temperature)      p is the output
  //     temperature(pressure, density)      p first
  //
  // It also reads in the order of the formula, T = p / (rho R). Recorded because
  // temperature(density, pressure) -- mirroring pressure()'s density-first order -- is
  // equally defensible, and a future reader should know a choice was made.
  //
  // Returned temperature is ABSOLUTE, and `pressure` is ABSOLUTE thermodynamic pressure,
  // the same quantity density() consumes -- never gauge. `referencePressure` remains the
  // single documented bridge (CompressibleSIMPLE.hpp, PhysicsConfig.hpp, ProjectRunner's
  // pressureAbsolute).
  //
  // On the abstraction rather than on IdealGasEOS for the same reason pressure() is: a
  // density-based solver recovering primitives from a conserved state must not have to
  // down-cast to a concrete EOS type to invert the relation.
  //
  // Throws InvalidArgumentError if p or rho is not finite and strictly positive, or if
  // the resulting temperature is not finite and strictly positive -- the same contract
  // density() and pressure() state, in the same order, with the same messages.
  [[nodiscard]] virtual Real temperature(Real pressure, Real density) const = 0;
};

}  // namespace cfd::compressible
