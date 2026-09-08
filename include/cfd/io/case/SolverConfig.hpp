#pragma once

#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::io {

// Mirrors cfd::algebra::LinearSolverSettings field-for-field -- kept as
// its own type rather than reusing LinearSolverSettings directly so the
// JSON-facing shape (which adds "type") and the solver-facing shape can
// evolve independently (TODO.md P1 section 17). type is always
// "BiCGSTAB" for the current numerical scope (the only Krylov solver
// SIMPLE actually uses).
struct LinearSolverSpec {
  std::string type;
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
