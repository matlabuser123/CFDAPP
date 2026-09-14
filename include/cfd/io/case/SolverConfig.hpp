#pragma once

#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/solver/SolverRobustness.hpp"

namespace cfd::io {

// Mirrors cfd::algebra::LinearSolverSettings field-for-field -- kept as
// its own type rather than reusing LinearSolverSettings directly so the
// JSON-facing shape (which adds "type"/"backend") and the solver-facing
// shape can evolve independently (TODO.md P1 section 17). type is one of
// "CG"/"BiCGSTAB"/"GMRES" (P12-NUM-004 added GMRES, CPU only) (P6-GPU-002 --
// previously always "BiCGSTAB", the only
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

  // P12-NUM-001: one of "upwind" (default) / "central" / "linear_upwind"
  // / "quick" -- mirrors cfd::discretization::ConvectionScheme. Optional
  // in solver.json (absent -> "upwind", the only scheme that existed
  // before this field, so every pre-P12-NUM-001 case file keeps parsing
  // into the exact same behavior).
  std::string convectionScheme{"upwind"};

  // P12-NUM-002: one of "green_gauss" (default) / "least_squares" --
  // mirrors cfd::discretization::GradientScheme. Optional in solver.json
  // (absent -> "green_gauss", the only scheme that existed before this
  // field, so every pre-P12-NUM-002 case file keeps parsing into the
  // exact same behavior).
  std::string gradientScheme{"green_gauss"};

  // P12-NUM-003: 0 (default) -- every pre-P12-NUM-003 case file keeps
  // parsing into exactly the uncorrected solver. N >= 1 enables the
  // non-orthogonal correction of every diffusion term and runs N
  // momentum-predictor and N pressure-correction passes per SIMPLE
  // iteration -- see pressure_velocity::SIMPLESettings::
  // nonOrthogonalCorrections. Must be >= 0.
  Index nonOrthogonalCorrections{0};

  // P12-NUM-004: solver.json's optional "robustness" block (see
  // docs/user_guide/case_format.md). Held directly as the solver-facing
  // settings type so the defaults exist in exactly one place; absent block
  // -> default-constructed = absolute convergence criterion and every
  // feature disabled, i.e. exactly the pre-P12-NUM-004 solver.
  cfd::solver::SolverRobustnessSettings robustness;
};

}  // namespace cfd::io
