#include "cfd/fields/ScalarField.hpp"

#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::fields {

namespace {

void checkSameSize(const ScalarField& a, const ScalarField& b) {
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

ScalarField& ScalarField::operator+=(const ScalarField& rhs) {
  checkSameSize(*this, rhs);
  for (size_type i = 0; i < size(); ++i) {
    (*this)[i] += rhs[i];
  }
  return *this;
}

ScalarField& ScalarField::operator-=(const ScalarField& rhs) {
  checkSameSize(*this, rhs);
  for (size_type i = 0; i < size(); ++i) {
    (*this)[i] -= rhs[i];
  }
  return *this;
}

ScalarField& ScalarField::operator*=(Real scalar) {
  for (Real& value : *this) {
    value *= scalar;
  }
  return *this;
}

ScalarField& ScalarField::operator/=(Real scalar) {
  checkNonZeroDivisor(scalar);
  for (Real& value : *this) {
    value /= scalar;
  }
  return *this;
}

ScalarField operator+(const ScalarField& lhs, const ScalarField& rhs) {
  ScalarField result = lhs;
  result += rhs;
  return result;
}

ScalarField operator-(const ScalarField& lhs, const ScalarField& rhs) {
  ScalarField result = lhs;
  result -= rhs;
  return result;
}

ScalarField operator*(const ScalarField& field, Real scalar) {
  ScalarField result = field;
  result *= scalar;
  return result;
}

ScalarField operator*(Real scalar, const ScalarField& field) { return field * scalar; }

ScalarField operator/(const ScalarField& field, Real scalar) {
  ScalarField result = field;
  result /= scalar;
  return result;
}

}  // namespace cfd::fields
