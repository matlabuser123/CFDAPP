#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::constants {

inline constexpr Real pi = 3.14159265358979323846;
inline constexpr Real twoPi = 2.0 * pi;
inline constexpr Real halfPi = 0.5 * pi;

// Safe small numerical values for divide-by-zero guards and floating-point
// comparisons across the codebase (mesh geometry, discretization,
// convergence checks, ...). Solver settings (tolerances, under-relaxation,
// max iterations, fluid properties, ...) do NOT belong here -- those are
// tunable case configuration, not universal constants.
inline constexpr Real tiny = 1.0e-30;
inline constexpr Real small = 1.0e-12;

}  // namespace cfd::constants
