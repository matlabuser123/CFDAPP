#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/Field.hpp"

namespace cfd::fields {

// One Vector2 value per mesh cell: velocity, gradient-like vector
// quantities, vector source terms, ... . 2D for the current phase; a
// Vector3-based generalization arrives alongside a 3D mesh.
class VectorField : public Field<Vector2> {
 public:
  using Field<Vector2>::Field;

  // Compound assignment validates sizes (or the divisor) before mutating
  // any element, so a failed operation leaves *this unchanged.
  VectorField& operator+=(const VectorField& rhs);
  VectorField& operator-=(const VectorField& rhs);
  VectorField& operator*=(Real scalar);
  VectorField& operator/=(Real scalar);
};

[[nodiscard]] VectorField operator+(const VectorField& lhs, const VectorField& rhs);
[[nodiscard]] VectorField operator-(const VectorField& lhs, const VectorField& rhs);
[[nodiscard]] VectorField operator*(const VectorField& field, Real scalar);
[[nodiscard]] VectorField operator*(Real scalar, const VectorField& field);
[[nodiscard]] VectorField operator/(const VectorField& field, Real scalar);

}  // namespace cfd::fields
