#pragma once

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::pressure_velocity {

// Every numerical control SIMPLE needs, gathered in one place rather
// than scattered as magic numbers through the iteration (TODO.md P0 --
// SIMPLE section 4). Defaults are reasonable starting points for a
// lid-driven cavity, not correctness guarantees -- validateSIMPLESettings
// only checks structural validity (finite, in-range), not "will this
// converge for your case".
struct SIMPLESettings {
  Index maxIterations{1000};

  Real velocityRelaxation{0.7};
  Real pressureRelaxation{0.3};

  Real velocityTolerance{1e-8};
  Real pressureTolerance{1e-8};
  Real continuityTolerance{1e-8};

  cfd::algebra::LinearSolverSettings momentumSolver;
  cfd::algebra::LinearSolverSettings pressureSolver;
};

// Throws InvalidArgumentError if:
//   - maxIterations == 0
//   - velocityRelaxation or pressureRelaxation is not finite, or not in
//     (0, 1] (TODO.md section 5: 0 < alpha <= 1, never silently clamped)
//   - any tolerance is not finite and > 0
void validateSIMPLESettings(const SIMPLESettings& settings);

}  // namespace cfd::pressure_velocity
