#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/Field.hpp"

namespace cfd::fields {

// One vector value per mesh cell: velocity, gradient-like vector
// quantities, vector source terms, ... . P12-MESH-005: always three
// components (u, v, w) -- Vector2 is an alias of Vector3 -- rather than a
// dimension-dependent size: on a 2D mesh w is 0 and every 2D operation is
// unchanged; on a 3D mesh the same field carries w. One field type for both
// dimensions, so no operator needs a 2D and a 3D version of its signature.
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
