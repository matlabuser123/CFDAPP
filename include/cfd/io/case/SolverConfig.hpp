#pragma once

#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::io {

// Mirrors cfd::algebra::LinearSolverSettings field-for-field -- kept as
// its own type rather than reusing LinearSolverSettings directly so the
// JSON-facing shape (which adds "type"/"backend") and the solver-facing
// shape can evolve independently (TODO.md P1 section 17). type is one of
// "CG"/"BiCGSTAB" (P6-GPU-002 -- previously always "BiCGSTAB", the only
// Krylov method SIMPLE constructed; CG is now equally constructible,
// though SIMPLE's own momentum/pressure systems are not SPD in general
// so BiCGSTAB remains the only well-posed choice for production cases --
// this codebase does not verify SPD-ness before running CG, matching
// cfd::algebra::CG's own documented "use only on matrices known to be
// SPD" contract). backend is "CPU" (default -- absent in every
// pre-P6-GPU-002 case file, which keeps parsing unchanged) or "GPU".
struct LinearSolverSpec {
  std::string type;
  std::string backend{"CPU"};
  Real absoluteTolerance{};
  Real relativeTolerance{};
  Index maxIterations{};
};

// Mirrors cfd::pressure_velocity::SIMPLESettings. type is always
// "SIMPLE".
struct SolverConfig {
  std::string type;
  Index maxIterations{};
  Real velocityRelaxation{};
  Real pressureRelaxation{};
  Real velocityTolerance{};
  Real pressureTolerance{};
  Real continuityTolerance{};
  LinearSolverSpec momentumSolver;
  LinearSolverSpec pressureSolver;
};

}  // namespace cfd::io
