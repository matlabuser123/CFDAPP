#pragma once

#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::species {

// P3-PHYS-004: identity + constant diffusivity for one transported,
// passive, non-reacting scalar species. Combines what the task spec's
// own section 3 sketches as two separate types (a name-only
// `SpeciesDefinition` plus a diffusivity model) into a single value type,
// the same "identity + constant coefficients, validated once at
// construction" design `cfd::thermal::ThermalProperties` already
// establishes for the closest architectural analogue (conductivity +
// specific heat) -- there is no genuine need for two classes here when
// one already fully captures what a species *is* for this task's scope
// (see this task's own section 5: "Do not force these exact classes if
// existing abstractions already cover the need").
//
// A field-per-species convention (this task's own section 3/5) means
// nothing here stores a concentration field: a caller transports as many
// independent species as it wants by holding one `cfd::fields::ScalarField`
// per species name and calling `SpeciesEquation`/`SpeciesSolver` once per
// species, each with its own `SpeciesProperties` -- no per-species solver
// class, no solver duplication (this task's own section 3 closing
// requirement), exactly mirroring how `ThermalSolver` is one class reused
// for however many independent thermal fields a caller wants (there
// happens to be only one in this codebase today, but nothing about
// ThermalSolver assumes that).
//
// The transported scalar convention (this task's own section 4) is left
// to the caller: this class does not itself enforce `0 <= Y <= 1` or
// `sum(Y_i) = 1` -- those are properties of a *mass-fraction* mixture
// convention, not of a generic constant-diffusivity passive scalar, and
// this task's own section 4 explicitly forbids enforcing `sum(Y)=1` for
// an arbitrary passive tracer. Boundedness (`0 <= Y <= 1`) is instead
// checked as a reporting-only diagnostic by callers that know their
// field is a genuine mass fraction (see
// `cfd::species::concentrationBounds` in SpeciesEquation.hpp).
class SpeciesProperties {
 public:
  // Throws InvalidArgumentError if name is empty, or diffusivity is not
  // finite or is negative. Zero diffusivity is deliberately accepted
  // (this task's own section 12: "D=0 should be allowed if pure
  // advection is intentionally supported") -- unlike
  // ThermalProperties's conductivity/specificHeat, which must be
  // strictly positive because this codebase's energy equation has no
  // meaningful pure-advection-only mode.
  SpeciesProperties(std::string name, Real diffusivity);

  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] Real diffusivity() const noexcept;

 private:
  std::string name_;
  Real diffusivity_{};
};

}  // namespace cfd::species
