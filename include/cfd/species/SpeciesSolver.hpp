#pragma once

#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/species/SpeciesProperties.hpp"

namespace cfd::species {

// P3-PHYS-004: the smallest clean solving layer on top of
// SpeciesEquation's assembly -- mirrors `cfd::thermal::ThermalSolver`
// exactly (this task's own section 2: "use the energy equation as the
// closest architectural analogue"), private outer-Picard-loop
// implementation included, for the identical reason
// ThermalSolverSettings's own header comment documents: a Neumann-style
// boundary (`FixedGradient`) is evaluated by SpeciesEquation against the
// *current* concentration field passed into assembly, so a single
// assemble-and-solve would leave such a boundary permanently anchored to
// the caller's initial guess. Re-assembling with the newly solved
// concentration each outer iteration converges this lag to
// self-consistency.
//
// One `SpeciesSolver` instance transports exactly one species per
// `solve()` call, driven by that call's own `SpeciesProperties` argument
// -- multiple independent passive species (this task's own section 3/27)
// are supported by calling `solve()` once per species with its own
// concentration field, never by a dedicated multi-species class (no
// solver duplication, same "one reusable class" precedent
// `cfd::thermal::ThermalSolver` already sets).
enum class SpeciesStatus {
  Converged,
  MaxIterations,
  LinearSolveFailure,
  NonFiniteState,
  InvalidConfiguration,
};

struct SpeciesSolverSettings {
  cfd::algebra::LinearSolverSettings linearSolver;
  Index maxIterations{2000};
  Real tolerance{1e-8};  // max absolute concentration change between outer iterations.
};

struct SpeciesResult {
  cfd::fields::ScalarField concentration;

  SpeciesStatus status{SpeciesStatus::MaxIterations};
  Index iterations{0};
  Index linearIterations{0};
  Real initialResidual{};
  Real finalResidual{};
  Real maxConcentrationChange{};
  std::vector<Real> residualHistory;
  std::vector<Real> outerChangeHistory;

  [[nodiscard]] bool converged() const noexcept { return status == SpeciesStatus::Converged; }
};

class SpeciesSolver {
 public:
  explicit SpeciesSolver(SpeciesSolverSettings settings = {});

  // Assembles species::assembleSpeciesTransportEquation against the given
  // massFlux/concentration boundaries/volumetric source and solves it
  // with BiCGSTAB (same solver choice as SIMPLE/ThermalSolver's own
  // convection-diffusion systems), repeating until the concentration
  // field stops changing -- see this class's own header comment for why
  // this outer loop exists. Never throws for a legitimate numerical-
  // failure outcome (invalid configuration, non-finite state, linear
  // solve failure, outer max-iterations) -- reported via `status`, same
  // convention as ThermalSolver::solve/SIMPLE::solve.
  [[nodiscard]] SpeciesResult solve(
      const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& initialConcentration,
      const cfd::fields::SurfaceField& massFlux, const cfd::physics::FluidProperties& fluid,
      const SpeciesProperties& species,
      const cfd::boundary::BoundaryConditionSet& concentrationBoundaries,
      Real volumetricSource = 0.0) const;

  [[nodiscard]] const SpeciesSolverSettings& settings() const noexcept;

 private:
  SpeciesSolverSettings settings_;
};

}  // namespace cfd::species
