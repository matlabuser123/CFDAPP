#pragma once

#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::multiphase {

// P3-PHYS-005: the smallest clean solving layer on top of
// VolumeFractionEquation's assembly -- a *single* implicit-Euler
// timestep, one linear solve, no outer Picard loop (unlike
// thermal::ThermalSolver/species::SpeciesSolver's own steady solve(),
// which iterate specifically to converge a lagged Neumann boundary to
// self-consistency; alpha transport's own boundary lag is bounded to one
// timestep by construction -- see VolumeFractionEquation.hpp's own header
// comment). This mirrors pressure_velocity::PISO's own "one-timestep
// solver, no TransientSolver wiring" scope boundary exactly (see
// src/CMakeLists.txt's own PISO-H/PISO-I comment) -- a caller advances
// multiple timesteps by calling step() repeatedly with the previous
// call's output as the next call's alphaOld, the same way this
// codebase's own natural-convection/Boussinesq validation tests own
// their outer coupling loop directly rather than requiring a fully wired
// production driver (a disclosed, deliberate scope boundary, not an
// oversight -- see TODO.md's own P3-PHYS-005 status note).
enum class VolumeFractionStatus {
  Converged,
  LinearSolveFailure,
  NonFiniteState,
  InvalidConfiguration,
};

struct VolumeFractionSolverSettings {
  cfd::algebra::LinearSolverSettings linearSolver;
};

struct VolumeFractionStepResult {
  cfd::fields::ScalarField alpha;

  VolumeFractionStatus status{VolumeFractionStatus::LinearSolveFailure};
  Index linearIterations{0};
  Real initialResidual{};
  Real finalResidual{};
  std::vector<Real> residualHistory;

  [[nodiscard]] bool converged() const noexcept {
    return status == VolumeFractionStatus::Converged;
  }
};

class VolumeFractionSolver {
 public:
  explicit VolumeFractionSolver(VolumeFractionSolverSettings settings = {});

  // Assembles multiphase::assembleVolumeFractionTransportEquation against
  // alphaOld/massFlux/alphaBoundaries/dt and solves it once with
  // BiCGSTAB (same solver choice as every other convection-bearing
  // system in this codebase). Never throws for a legitimate numerical-
  // failure outcome -- reported via `status`, same convention as
  // ThermalSolver::solve/SpeciesSolver::solve/SIMPLE::solve.
  [[nodiscard]] VolumeFractionStepResult step(
      const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& alphaOld,
      const cfd::fields::SurfaceField& massFlux,
      const cfd::boundary::BoundaryConditionSet& alphaBoundaries, Real dt) const;

  [[nodiscard]] const VolumeFractionSolverSettings& settings() const noexcept;

 private:
  VolumeFractionSolverSettings settings_;
};

}  // namespace cfd::multiphase
