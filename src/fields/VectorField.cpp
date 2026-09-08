#include "cfd/fields/VectorField.hpp"

#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::fields {

namespace {

void checkSameSize(const VectorField& a, const VectorField& b) {
  if (a.size() != b.size()) {
    throw InvalidArgumentError("Field size mismatch: " + std::to_string(a.size()) + " vs " +
                               std::to_string(b.size()));
  }
}

void checkNonZeroDivisor(Real scalar) {
  if (scalar == Real{0}) {
    throw InvalidArgumentError("Division by zero in field arithmetic");
  }
}

}  // namespace

VectorField& VectorField::operator+=(const VectorField& rhs) {
  checkSameSize(*this, rhs);
  for (size_type i = 0; i < size(); ++i) {
    (*this)[i] += rhs[i];
  }
  return *this;
}

VectorField& VectorField::operator-=(const VectorField& rhs) {
  checkSameSize(*this, rhs);
  for (size_type i = 0; i < size(); ++i) {
    (*this)[i] -= rhs[i];
  }
  return *this;
}

VectorField& VectorField::operator*=(Real scalar) {
  for (Vector2& value : *this) {
    value *= scalar;
  }
  return *this;
}

VectorField& VectorField::operator/=(Real scalar) {
  checkNonZeroDivisor(scalar);
  const Real inverse = 1.0 / scalar;
  for (Vector2& value : *this) {
    value *= inverse;
  }
  return *this;
}

VectorField operator+(const VectorField& lhs, const VectorField& rhs) {
  VectorField result = lhs;
  result += rhs;
  return result;
}

VectorField operator-(const VectorField& lhs, const VectorField& rhs) {
  VectorField result = lhs;
  result -= rhs;
  return result;
}

VectorField operator*(const VectorField& field, Real scalar) {
  VectorField result = field;
  result *= scalar;
  return result;
}

VectorField operator*(Real scalar, const VectorField& field) { return field * scalar; }

VectorField operator/(const VectorField& field, Real scalar) {
  VectorField result = field;
  result /= scalar;
  return result;
}

}  // namespace cfd::fields
