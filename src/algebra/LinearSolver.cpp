#include "cfd/algebra/LinearSolver.hpp"

#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

namespace {

void validateSettings(const LinearSolverSettings& settings) {
  if (settings.absoluteTolerance < 0.0) {
    throw InvalidArgumentError("LinearSolverSettings: absoluteTolerance must be >= 0");
  }
  if (settings.relativeTolerance < 0.0) {
    throw InvalidArgumentError("LinearSolverSettings: relativeTolerance must be >= 0");
  }
  if (settings.absoluteTolerance == 0.0 && settings.relativeTolerance == 0.0) {
    throw InvalidArgumentError(
        "LinearSolverSettings: at least one of absoluteTolerance/relativeTolerance must be > 0");
  }
  if (settings.maxIterations == 0) {
    throw InvalidArgumentError("LinearSolverSettings: maxIterations must be >= 1");
  }
}

}  // namespace

LinearSolver::LinearSolver(LinearSolverSettings settings) : settings_(settings) {
  validateSettings(settings_);
}

SolverResult LinearSolver::solve(const LinearSystem& system) const {
  return solve(system, Vector(system.size(), 0.0));
}

const LinearSolverSettings& LinearSolver::settings() const noexcept { return settings_; }

}  // namespace cfd::algebra
