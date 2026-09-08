#pragma once

#include <cmath>

#include "cfd/core/Types.hpp"

namespace cfd {

// A fixed-size 2D geometry vector/point: cell centroids, face centroids,
// and face area vectors in the 2D mesh layer. Deliberately not
// std::vector<Real> -- geometry coordinates are always exactly 2
// components, so a fixed-size type is both faster and cannot be
// accidentally constructed with the wrong size.
struct Vector2 {
  Real x{};
  Real y{};

  Vector2& operator+=(const Vector2& other) noexcept {
    x += other.x;
    y += other.y;
    return *this;
  }

  Vector2& operator-=(const Vector2& other) noexcept {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  Vector2& operator*=(Real scalar) noexcept {
    x *= scalar;
    y *= scalar;
    return *this;
  }
};

[[nodiscard]] inline Vector2 operator+(Vector2 a, const Vector2& b) noexcept {
  a += b;
  return a;
}

[[nodiscard]] inline Vector2 operator-(Vector2 a, const Vector2& b) noexcept {
  a -= b;
  return a;
}

[[nodiscard]] inline Vector2 operator-(const Vector2& a) noexcept { return Vector2{-a.x, -a.y}; }

[[nodiscard]] inline Vector2 operator*(Vector2 v, Real scalar) noexcept {
  v *= scalar;
  return v;
}

[[nodiscard]] inline Vector2 operator*(Real scalar, Vector2 v) noexcept {
  v *= scalar;
  return v;
}

[[nodiscard]] inline Real dot(const Vector2& a, const Vector2& b) noexcept {
  return (a.x * b.x) + (a.y * b.y);
}

[[nodiscard]] inline Real magnitude(const Vector2& v) noexcept { return std::sqrt(dot(v, v)); }

[[nodiscard]] inline bool operator==(const Vector2& a, const Vector2& b) noexcept {
  return a.x == b.x && a.y == b.y;
}

}  // namespace cfd
