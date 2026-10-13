#pragma once

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/physics/TemperatureProperty.hpp"

// P15 -- temperature-dependent material properties.
//
// WHAT THE AUDIT FOUND, AND WHAT THIS ADDS
// ----------------------------------------
// Spatially varying properties ALREADY WORK: EnergyEquation has ScalarField
// overloads for conductivity and specific heat, and ThermalSolver already runs
// a Picard loop over them. What did not exist was any MODEL producing the
// variation -- zero matches for k(T), cp(T) or "polynomial" anywhere in the
// thermal module or the case parser. A caller had to compute the field itself,
// and a case file could only give one scalar.
//
// So this supplies the missing half: a validated polynomial in temperature,
// evaluated into the fields the existing assembly already consumes. No
// assembler changes; nothing that worked stops working.
//
//   k(T)  = a0 + a1 T + a2 T^2 + ...
//   cp(T) = b0 + b1 T + b2 T^2 + ...
//   mu(T), rho(T) likewise
//
// A CONSTANT property is the single-coefficient case {a0}, so the constant and
// varying paths are one code path rather than two -- which means the constant
// case cannot drift away from the varying one.
//
// EVALUATION ORDER IS FIXED AND DOCUMENTED. Horner from the highest
// coefficient down:
//
//   result = a_n;  for i = n-1 .. 0:  result = result * T + a_i
//
// Stated because a polynomial summed in a different order gives different bits,
// and this project compares bits. Horner is also the numerically better choice,
// so there is no tension between the two reasons.

namespace cfd::thermal {

// A property as a polynomial in temperature, with an explicit validity range.
//
// The range is not decoration. Extrapolating a fitted polynomial outside the
// data it came from produces confident nonsense -- a cubic cp fit can go
// NEGATIVE a few hundred kelvin past its range, which makes enthalpy
// non-monotonic and the T(h) inversion ill-posed. Evaluation outside the range
// is therefore an error by default, not a silent extrapolation.
// Implements cfd::physics::TemperatureProperty (P3-PHYS-003) so the solver can
// consume it directly.
//
// That interface, evaluatePropertyField, and ThermalSolver::solve's
// variable-property overload all predate P15 and are already validated; what
// did not exist was a model with an integrable closed form (SensibleEnthalpy
// needs the coefficients symbolically, which a virtual value(T) cannot give).
// Deriving here rather than adding a parallel property hierarchy is the whole
// point: one property interface, one solver path, one Picard loop.
class TemperatureDependentProperty final : public cfd::physics::TemperatureProperty {
 public:
  // `coefficients` is ascending: {a0, a1, a2, ...}. At least one is required.
  //
  // Throws InvalidArgumentError if the list is empty, any coefficient is not
  // finite, the range is inverted or not finite, or the polynomial is not
  // strictly positive somewhere in the range -- a negative conductivity or
  // specific heat is not a physical material, and admitting one here would
  // push the failure into a linear solve where its cause is unrecognisable.
  TemperatureDependentProperty(std::vector<Real> coefficients, Real minimumTemperature,
                               Real maximumTemperature, std::string name);

  // A constant property: the single-coefficient case, valid everywhere.
  [[nodiscard]] static TemperatureDependentProperty constant(Real value, std::string name);

  // Horner evaluation. Throws InvalidArgumentError if `temperature` is outside
  // the validity range or not finite.
  [[nodiscard]] Real evaluate(Real temperature) const;

  // cfd::physics::TemperatureProperty. Exactly evaluate() -- same bits, same
  // out-of-range rejection, so a caller cannot get one answer through the
  // interface and a different one through the concrete type.
  //
  // Throwing out of range is deliberate and is NOT the clamping policy
  // TabulatedProperty documents. ThermalSolver's variable-property overload
  // already catches InvalidArgumentError out of evaluatePropertyField and
  // reports it as ThermalStatus::NonFiniteState, so an excursion past the
  // fitted range surfaces as a failed solve naming the property -- rather
  // than as a silently extrapolated value, which for a fitted polynomial can
  // be negative and makes the enthalpy inversion ill-posed.
  [[nodiscard]] Real value(Real temperature) const override;

  // Whole-field evaluation, for the ScalarField overloads the energy equation
  // already takes. Throws if any cell temperature is out of range, naming the
  // cell and the value -- a diagnostic a user can act on, per P13-002.
  [[nodiscard]] cfd::fields::ScalarField evaluateField(
      const cfd::fields::ScalarField& temperature) const;

  [[nodiscard]] bool isConstant() const noexcept { return coefficients_.size() == 1; }
  [[nodiscard]] const std::vector<Real>& coefficients() const noexcept { return coefficients_; }
  [[nodiscard]] Real minimumTemperature() const noexcept { return minimumTemperature_; }
  [[nodiscard]] Real maximumTemperature() const noexcept { return maximumTemperature_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }

 private:
  std::vector<Real> coefficients_;
  Real minimumTemperature_;
  Real maximumTemperature_;
  std::string name_;
};

}  // namespace cfd::thermal
