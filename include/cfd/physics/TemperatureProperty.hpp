#pragma once

#include <memory>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::physics {

// P3-PHYS-003: a reusable temperature-dependent property-model
// abstraction, shared by every property this codebase's equations
// consume as a single scalar today (mu, k, cp -- and, where physically
// appropriate, rho for post-processing/thermal-diffusivity purposes,
// never for continuity -- see this header's own "not used for
// continuity" note below). The solver-facing consumer (equation
// assembly) only ever calls `value(T)`; it does not know or care
// whether that came from a constant, a linear law, or a lookup table --
// same "solver consumes a value, does not know how it was produced"
// separation this project already uses for
// cfd::boundary::ScalarBoundaryCondition/BoundaryCondition.
class TemperatureProperty {
 public:
  virtual ~TemperatureProperty() = default;

  // P(T). Throws InvalidArgumentError if temperature is not finite.
  // Implementations do not themselves enforce positivity (a property
  // model is a pure mathematical law, valid or not depending on what
  // temperature range it is evaluated over) -- positivity is checked at
  // the point a property is actually evaluated into a field consumed by
  // an equation, by evaluatePropertyField() below (P3-PHYS-003 section
  // 5's own "reject during validation when possible, else fail clearly
  // during evaluation").
  [[nodiscard]] virtual Real value(Real temperature) const = 0;
};

// P(T) = P_ref, independent of temperature -- the mandatory backward-
// compatibility model (P3-PHYS-003 section 4): every existing constant-
// property code path is exactly reproduced by wrapping its own fixed
// value in this class. Throws InvalidArgumentError if referenceValue is
// not finite.
class ConstantProperty final : public TemperatureProperty {
 public:
  explicit ConstantProperty(Real referenceValue);
  [[nodiscard]] Real value(Real temperature) const override;

  [[nodiscard]] Real referenceValue() const noexcept;

 private:
  Real referenceValue_{};
};

// P(T) = P_ref + slope*(T - T_ref) -- the canonical linear model chosen
// for this task (P3-PHYS-003 section 5's "P_ref + slope*(T-T_ref)"
// form, preferred here over the multiplicative "P_ref*[1+a*(T-T_ref)]"
// alternative the task also offers, since it is linear in `slope`
// directly rather than requiring slope=a*P_ref to compare against a
// hand-derived expectation -- simpler to hand-verify, and the two forms
// are trivially interconvertible, a = slope/P_ref). Throws
// InvalidArgumentError if referenceValue, referenceTemperature, or
// slope is not finite.
class LinearProperty final : public TemperatureProperty {
 public:
  LinearProperty(Real referenceValue, Real referenceTemperature, Real slope);
  [[nodiscard]] Real value(Real temperature) const override;

  [[nodiscard]] Real referenceValue() const noexcept;
  [[nodiscard]] Real referenceTemperature() const noexcept;
  [[nodiscard]] Real slope() const noexcept;

 private:
  Real referenceValue_{};
  Real referenceTemperature_{};
  Real slope_{};
};

// Deterministic linear interpolation of a tabulated (T, P) dataset.
// Requires: at least 2 points, temperatures strictly increasing, every
// value finite -- all checked at construction (throws
// InvalidArgumentError, naming which invariant failed). Exact node
// recovery: value(temperatures[i]) == values[i] identically (not just
// approximately) for every table node, since a query landing exactly on
// a node interpolates between that node and itself (weight 0 or 1,
// either way returning the stored value bit-for-bit).
//
// Out-of-range policy (P3-PHYS-003 section 6's own "document and test
// whichever policy is selected"): **constant endpoint extrapolation**
// (clamp to the first/last table value) -- chosen over rejecting an
// out-of-range query because every call site here is deep inside an
// outer nonlinear (Picard) iteration; a query landing a hair outside
// the table during an intermediate, not-yet-converged iterate is a
// normal, expected transient, not a configuration error, and raising
// there would make the whole calling equation assembly fail
// unpredictably depending on iteration history. A genuinely invalid
// case (the *converged* temperature range never intended to leave the
// table) is instead something a caller can and should check separately
// against the table's own tableMinTemperature()/tableMaxTemperature().
class TabulatedProperty final : public TemperatureProperty {
 public:
  TabulatedProperty(std::vector<Real> temperatures, std::vector<Real> values);
  [[nodiscard]] Real value(Real temperature) const override;

  [[nodiscard]] Real tableMinTemperature() const noexcept;
  [[nodiscard]] Real tableMaxTemperature() const noexcept;

 private:
  std::vector<Real> temperatures_;
  std::vector<Real> values_;
};

// Evaluates `property` at every cell's current temperature, returning a
// per-cell ScalarField the field-based equation-assembly overloads
// (MomentumEquation.hpp's existing effectiveViscosity-field overload;
// EnergyEquation.hpp's new conductivity-/specificHeat-field overloads)
// consume directly. When `requirePositive` is true (the default -- every
// one of mu/k/cp/rho this codebase's equations divide or multiply by
// must be strictly positive, P3-PHYS-003 section 5's own explicit
// example), throws InvalidArgumentError naming the offending cell if
// any evaluated value is non-finite or <= 0 -- evaluated *here*, at the
// one point a property model's output enters the field the rest of the
// solver consumes, the same "validate at the entry point" convention
// FluidProperties's own constructor and assembleDiffusionContribution's
// own effectiveViscosity-field overload already apply. Pass
// requirePositive=false only for a property this codebase's own
// equations do not require positive (none currently -- kept as an
// explicit opt-out rather than hardcoding positivity into every call
// site). Throws InvalidArgumentError if temperature.size() !=
// mesh.numberOfCells().
[[nodiscard]] cfd::fields::ScalarField evaluatePropertyField(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature,
    const TemperatureProperty& property, bool requirePositive = true);

}  // namespace cfd::physics
