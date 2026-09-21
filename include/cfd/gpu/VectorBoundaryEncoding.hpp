#pragma once

// GPU-DISC-001D closure -- host-side encoding of a VectorBoundaryCondition so a
// kernel can evaluate it without a virtual call.
//
// The vector counterpart of BoundaryEncoding.hpp, and built to the same rule:
// the device must reproduce the condition's ARITHMETIC, not merely its value in
// exact arithmetic, and whichever form is chosen is verified by exact bit
// comparison against the condition itself.
//
// Every vector condition in the codebase falls into one of three forms, with
// `n` the face unit normal and `u` the owner-cell velocity:
//
//   constant    value = c              Wall ({0,0,0}), MovingWall, Inlet
//   identity    value = u              Outlet (zero-gradient)
//   symmetry    value = u - (n * dot(u, n))   Symmetry
//
// The symmetry form is NOT folded into a 3x3 matrix-times-vector. Symmetry.cpp
// computes one guarded dot product and one component-wise multiply-subtract;
// evaluating the equivalent matrix by row dot products rounds differently, and
// the gate here is bitwise.
//
// This is deliberately the MINIMUM needed by the momentum convection
// contribution. It is not general CUDA boundary-condition support -- the
// `Boundary conditions` TODO item covers that and stays unchecked -- but this
// encoder is written to be reused there.

#include <cstring>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::gpu {

inline constexpr cfd::Index kVectorBoundaryConstant = 0;
inline constexpr cfd::Index kVectorBoundaryIdentity = 1;
inline constexpr cfd::Index kVectorBoundarySymmetry = 2;

// `normal` must be the same MeshGeometry::unitNormal(face) the CPU passes, and
// `distance` the same owner-to-face distance. On success `constantValue` holds
// the constant form's c (zero for the other two kinds).
inline bool encodeVectorBoundaryCondition(const cfd::boundary::VectorBoundaryCondition& bc,
                                          cfd::Real distance, const cfd::Vector2& normal,
                                          cfd::Vector2& constantValue, cfd::Index& kind) {
  using cfd::Real;
  using cfd::Vector2;
  const Vector2 probes[] = {
      Vector2{0.0, 0.0, 0.0},   Vector2{1.0, 0.0, 0.0},    Vector2{0.0, 1.0, 0.0},
      Vector2{0.0, 0.0, 1.0},   Vector2{-1.0, 2.5, -0.25}, Vector2{3.5, -7.125, 0.5},
      Vector2{1e3, -1e-3, 2.0}, Vector2{0.3, 0.7, -0.9},   Vector2{-2.75, -0.125, 4.0},
  };
  const auto reproduces = [&](const auto& evaluate) {
    for (const Vector2& u : probes) {
      const Vector2 expected = bc.boundaryValue(u, distance, normal);
      const Vector2 encoded = evaluate(u);
      if (std::memcmp(&expected.x, &encoded.x, sizeof(Real)) != 0) return false;
      if (std::memcmp(&expected.y, &encoded.y, sizeof(Real)) != 0) return false;
      if (std::memcmp(&expected.z, &encoded.z, sizeof(Real)) != 0) return false;
    }
    return true;
  };

  const Vector2 candidate = bc.boundaryValue(Vector2{0.0, 0.0, 0.0}, distance, normal);
  if (reproduces([&](const Vector2&) { return candidate; })) {
    constantValue = candidate;
    kind = kVectorBoundaryConstant;
    return true;
  }
  if (reproduces([&](const Vector2& u) { return u; })) {
    constantValue = Vector2{0.0, 0.0, 0.0};
    kind = kVectorBoundaryIdentity;
    return true;
  }
  if (reproduces([&](const Vector2& u) { return u - (normal * dot(u, normal)); })) {
    constantValue = Vector2{0.0, 0.0, 0.0};
    kind = kVectorBoundarySymmetry;
    return true;
  }
  return false;
}

}  // namespace cfd::gpu
