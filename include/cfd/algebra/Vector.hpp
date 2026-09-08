#pragma once

#include <initializer_list>
#include <vector>

#include "cfd/core/Types.hpp"

namespace cfd::algebra {

// Dense vector of Real values: the algebraic unknown/right-hand-side
// vectors used by linear solvers (velocity-component unknowns, pressure
// correction, temperature, ...). Deliberately independent of Mesh/Field --
// the algebra layer is testable without a CFD mesh.
class Vector {
 public:
  using size_type = std::size_t;
  using iterator = std::vector<Real>::iterator;
  using const_iterator = std::vector<Real>::const_iterator;

  Vector() = default;
  explicit Vector(size_type size);
  Vector(size_type size, Real initialValue);
  Vector(std::initializer_list<Real> values);

  [[nodiscard]] size_type size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  Real& operator[](size_type index) noexcept;
  [[nodiscard]] const Real& operator[](size_type index) const noexcept;

  Real& at(size_type index);
  [[nodiscard]] const Real& at(size_type index) const;

  Real* data() noexcept;
  [[nodiscard]] const Real* data() const noexcept;

  void fill(Real value);

  // Never silently ignores NaN/Inf -- solvers call this before/while
  // iterating rather than letting non-finite values propagate.
  [[nodiscard]] bool allFinite() const noexcept;

  iterator begin() noexcept;
  iterator end() noexcept;
  [[nodiscard]] const_iterator begin() const noexcept;
  [[nodiscard]] const_iterator end() const noexcept;

  // Validates sizes (or the divisor) before mutating any element, so a
  // failed operation leaves *this unchanged.
  Vector& operator+=(const Vector& rhs);
  Vector& operator-=(const Vector& rhs);
  Vector& operator*=(Real scalar);
  Vector& operator/=(Real scalar);

 private:
  std::vector<Real> values_;
};

[[nodiscard]] Vector operator+(const Vector& lhs, const Vector& rhs);
[[nodiscard]] Vector operator-(const Vector& lhs, const Vector& rhs);
[[nodiscard]] Vector operator*(const Vector& v, Real scalar);
[[nodiscard]] Vector operator*(Real scalar, const Vector& v);
[[nodiscard]] Vector operator/(const Vector& v, Real scalar);

// Rejects mismatched sizes (throws InvalidArgumentError); never truncates.
[[nodiscard]] Real dot(const Vector& a, const Vector& b);

[[nodiscard]] Real l1Norm(const Vector& v) noexcept;
[[nodiscard]] Real l2Norm(const Vector& v) noexcept;
[[nodiscard]] Real infinityNorm(const Vector& v) noexcept;

}  // namespace cfd::algebra
