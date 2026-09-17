#pragma once

#include <cmath>

#include "cfd/core/Types.hpp"

namespace cfd {

// A fixed-size geometry vector/point: cell centroids, face centroids, face
// area vectors, velocities and gradients. Deliberately not
// std::vector<Real> -- geometry coordinates always have exactly three
// components, so a fixed-size type is both faster and cannot be
// accidentally constructed with the wrong size.
//
// P12-MESH-005: the one vector type of both the 2D and the 3D mesh layer
// (Vector2 is an alias of it, see Vector2.hpp). A 2D mesh stores z = 0 in
// every point and vector, and every operation below returns bit for bit the
// value the former two-component Vector2 returned for x and y (see dot):
// two-dimensional results are unchanged, not merely unchanged to round-off.
struct Vector3 {
  Real x{};
  Real y{};
  Real z{};

  Vector3& operator+=(const Vector3& other) noexcept {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }

  Vector3& operator-=(const Vector3& other) noexcept {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
  }

  Vector3& operator*=(Real scalar) noexcept {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
  }
};

[[nodiscard]] inline Vector3 operator+(Vector3 a, const Vector3& b) noexcept {
  a += b;
  return a;
}

[[nodiscard]] inline Vector3 operator-(Vector3 a, const Vector3& b) noexcept {
  a -= b;
  return a;
}

[[nodiscard]] inline Vector3 operator-(const Vector3& a) noexcept {
  return Vector3{-a.x, -a.y, -a.z};
}

[[nodiscard]] inline Vector3 operator*(Vector3 v, Real scalar) noexcept {
  v *= scalar;
  return v;
}

[[nodiscard]] inline Vector3 operator*(Real scalar, Vector3 v) noexcept {
  v *= scalar;
  return v;
}

// a.x b.x + a.y b.y + a.z b.z, with one deliberate refinement: a z product
// that is exactly zero contributes nothing, instead of being added. The two
// agree except in the sign of a zero result (IEEE -0.0 + +0.0 = +0.0), so
// this changes no value -- but it makes the dot product of two 2D vectors
// (z = 0) bitwise identical to the former two-term formula, including a
// -0.0 result, which a later atan2/copysign/printout could otherwise
// distinguish.
[[nodiscard]] inline Real dot(const Vector3& a, const Vector3& b) noexcept {
  const Real inPlane = (a.x * b.x) + (a.y * b.y);
  const Real normal = a.z * b.z;
  return normal == 0.0 ? inPlane : inPlane + normal;
}

// The cross product a x b. For two 2D vectors its z component is exactly the
// former 2D "cross product" (a.x b.y) - (a.y b.x) and its x, y components are
// zero.
[[nodiscard]] inline Vector3 cross(const Vector3& a, const Vector3& b) noexcept {
  return Vector3{(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

[[nodiscard]] inline Real magnitude(const Vector3& v) noexcept { return std::sqrt(dot(v, v)); }

[[nodiscard]] inline bool operator==(const Vector3& a, const Vector3& b) noexcept {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

[[nodiscard]] inline bool isFinite(const Vector3& v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace cfd
