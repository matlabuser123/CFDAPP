#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/fields/Field.hpp"

namespace cfd::fields {

// One Real value per mesh cell: pressure, density, viscosity, temperature,
// turbulent kinetic energy, residual-like scalar quantities, ... . A thin,
// named specialization of Field<Real> -- kept as its own class (not a bare
// alias) so it can grow scalar-specific operations later without changing
// the public API shape.
class ScalarField : public Field<Real> {
 public:
  using Field<Real>::Field;

  // Compound assignment validates sizes (or the divisor) before mutating
  // any element, so a failed operation leaves *this unchanged.
  ScalarField& operator+=(const ScalarField& rhs);
  ScalarField& operator-=(const ScalarField& rhs);
  ScalarField& operator*=(Real scalar);
  ScalarField& operator/=(Real scalar);
};

[[nodiscard]] ScalarField operator+(const ScalarField& lhs, const ScalarField& rhs);
[[nodiscard]] ScalarField operator-(const ScalarField& lhs, const ScalarField& rhs);
[[nodiscard]] ScalarField operator*(const ScalarField& field, Real scalar);
[[nodiscard]] ScalarField operator*(Real scalar, const ScalarField& field);
[[nodiscard]] ScalarField operator/(const ScalarField& field, Real scalar);

}  // namespace cfd::fields
