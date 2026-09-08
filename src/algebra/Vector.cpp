#include "cfd/algebra/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

namespace {

void checkSameSize(const Vector& a, const Vector& b, const char* op) {
  if (a.size() != b.size()) {
    throw InvalidArgumentError(std::string("Vector size mismatch in ") + op + ": " +
                               std::to_string(a.size()) + " vs " + std::to_string(b.size()));
  }
}

void checkNonZeroDivisor(Real scalar) {
  if (scalar == Real{0}) {
    throw InvalidArgumentError("Division by zero in Vector arithmetic");
  }
}

}  // namespace

Vector::Vector(size_type size) : values_(size) {}
Vector::Vector(size_type size, Real initialValue) : values_(size, initialValue) {}
Vector::Vector(std::initializer_list<Real> values) : values_(values) {}

Vector::size_type Vector::size() const noexcept { return values_.size(); }
bool Vector::empty() const noexcept { return values_.empty(); }

Real& Vector::operator[](size_type index) noexcept { return values_[index]; }
const Real& Vector::operator[](size_type index) const noexcept { return values_[index]; }

Real& Vector::at(size_type index) { return values_.at(index); }
const Real& Vector::at(size_type index) const { return values_.at(index); }

Real* Vector::data() noexcept { return values_.data(); }
const Real* Vector::data() const noexcept { return values_.data(); }

void Vector::fill(Real value) { std::fill(values_.begin(), values_.end(), value); }

bool Vector::allFinite() const noexcept {
  for (const Real value : values_) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

Vector::iterator Vector::begin() noexcept { return values_.begin(); }
Vector::iterator Vector::end() noexcept { return values_.end(); }
Vector::const_iterator Vector::begin() const noexcept { return values_.begin(); }
Vector::const_iterator Vector::end() const noexcept { return values_.end(); }

Vector& Vector::operator+=(const Vector& rhs) {
  checkSameSize(*this, rhs, "operator+=");
  for (size_type i = 0; i < size(); ++i) {
    values_[i] += rhs[i];
  }
  return *this;
}

Vector& Vector::operator-=(const Vector& rhs) {
  checkSameSize(*this, rhs, "operator-=");
  for (size_type i = 0; i < size(); ++i) {
    values_[i] -= rhs[i];
  }
  return *this;
}

Vector& Vector::operator*=(Real scalar) {
  for (Real& value : values_) {
    value *= scalar;
  }
  return *this;
}

Vector& Vector::operator/=(Real scalar) {
  checkNonZeroDivisor(scalar);
  for (Real& value : values_) {
    value /= scalar;
  }
  return *this;
}

Vector operator+(const Vector& lhs, const Vector& rhs) {
  Vector result = lhs;
  result += rhs;
  return result;
}

Vector operator-(const Vector& lhs, const Vector& rhs) {
  Vector result = lhs;
  result -= rhs;
  return result;
}

Vector operator*(const Vector& v, Real scalar) {
  Vector result = v;
  result *= scalar;
  return result;
}

Vector operator*(Real scalar, const Vector& v) { return v * scalar; }

Vector operator/(const Vector& v, Real scalar) {
  Vector result = v;
  result /= scalar;
  return result;
}

Real dot(const Vector& a, const Vector& b) {
  checkSameSize(a, b, "dot");
  Real sum = 0.0;
  for (Vector::size_type i = 0; i < a.size(); ++i) {
    sum += a[i] * b[i];
  }
  return sum;
}

Real l1Norm(const Vector& v) noexcept {
  Real sum = 0.0;
  for (const Real value : v) {
    sum += std::abs(value);
  }
  return sum;
}

Real l2Norm(const Vector& v) noexcept {
  Real sum = 0.0;
  for (const Real value : v) {
    sum += value * value;
  }
  return std::sqrt(sum);
}

Real infinityNorm(const Vector& v) noexcept {
  Real maxAbs = 0.0;
  for (const Real value : v) {
    maxAbs = std::max(maxAbs, std::abs(value));
  }
  return maxAbs;
}

}  // namespace cfd::algebra
