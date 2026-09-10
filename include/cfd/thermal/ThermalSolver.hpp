#pragma once

#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/thermal/ThermalProperties.hpp"
#include "cfd/thermal/ThermalRegionMap.hpp"

namespace cfd::thermal {

// P2-THERMAL-004: the smallest clean solving layer on top of
// EnergyEquation's assembly. Evidence, not just a converged-or-not flag,
// same philosophy as pressure_velocity::SIMPLEStatus/SIMPLEResult: a
// failure category is never masked as a generic failure.
enum class ThermalStatus {
  Converged,
  MaxIterations,
  LinearSolveFailure,
  NonFiniteState,
  InvalidConfiguration,
};

// An *outer* Picard/fixed-point loop is required, not optional, even
// though the energy equation itself is linear for fixed properties and a
// fixed velocity field. Reason (found by diagnosing a real convergence
// failure while validating this class, not assumed up front): boundary
// conditions whose face value depends on the owner cell's current state
// (Adiabatic, HeatFlux, and any other Neumann-family
// ScalarBoundaryCondition) are evaluated by EnergyEquation against the
// *current* temperature field passed into assembly -- the same
// evaluate-at-current-state convention physics::MomentumEquation already
// uses for Outlet/Symmetry. For momentum, this is exact once SIMPLE's
// outer loop has converged (it re-assembles every iteration with the
// latest velocity); a *single* assemble-and-solve here would instead
// leave every such boundary permanently anchored to the caller's initial
// guess, never actually reaching the true zero-flux/prescribed-flux
// condition. Re-assembling with the newly solved temperature each outer
// iteration converges this lag to self-consistency -- verified to
// reproduce the exact analytical 1D-conduction profile (see
// test_thermal_solver.cpp), and empirically fast (a few hundred cheap
// outer iterations even at 80x80 resolution, well inside the default
// budget below) since only the boundary evaluation is lagged, not the
// whole equation.
struct ThermalSolverSettings {
  cfd::algebra::LinearSolverSettings linearSolver;
  Index maxIterations{2000};
  Real tolerance{1e-8};  // max absolute temperature change between outer iterations.
};

// linearIterations/initialResidual/finalResidual/residualHistory describe
// the *last* outer iteration's inner linear solve (the one whose solution
// this result reports) -- there is deliberately no per-outer-iteration
// linear-solve history stored (would grow with maxIterations for little
// benefit); outerChangeHistory below is the per-outer-iteration
// convergence record instead. iterations is the number of outer
// (assemble + linear-solve) iterations actually run.
struct ThermalResult {
  cfd::fields::ScalarField temperature;

  ThermalStatus status{ThermalStatus::MaxIterations};
  Index iterations{0};
  Index linearIterations{0};
  Real initialResidual{};
  Real finalResidual{};
  Real maxTemperatureChange{};
  std::vector<Real> residualHistory;
  std::vector<Real> outerChangeHistory;

  [[nodiscard]] bool converged() const noexcept { return status == ThermalStatus::Converged; }
};

class ThermalSolver {
 public:
  explicit ThermalSolver(ThermalSolverSettings settings = {});

  // Assembles the steady energy equation (thermal::assembleEnergyEquation)
  // against the given massFlux/temperature boundaries/heat source and
  // solves it with BiCGSTAB (general non-symmetric solver, matching
  // SIMPLE's own choice for its convection-diffusion systems -- CG's SPD
  // requirement does not hold once convection is present), repeating
  // (re-assembling against the newly solved field) until the temperature
  // field stops changing -- see ThermalSolverSettings's own comment for
  // why this outer loop exists. Never throws for a legitimate
  // numerical-failure outcome (invalid configuration, non-finite state,
  // linear solve failure, outer max-iterations) -- these are all reported
  // via `status`, the same convention as pressure_velocity::SIMPLE::solve.
  [[nodiscard]] ThermalResult solve(
      const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& initialTemperature,
      const cfd::fields::SurfaceField& massFlux, const ThermalProperties& thermal,
      const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
      Real volumetricHeatSource = 0.0) const;

  // P3-PHYS-003: the temperature-dependent-property counterpart of
  // solve() above -- same single-material convection-diffusion equation
  // and the exact same outer Picard loop (runPicardLoop), but conductivity/
  // specificHeat are no longer fixed for the whole solve: `conductivityModel`/
  // `specificHeatModel` are re-evaluated into fresh per-cell fields (via
  // cfd::physics::evaluatePropertyField) from the *current* temperature
  // iterate at the start of every outer iteration, inside the same
  // `assemble` lambda mechanism solve() above already uses -- no second,
  // independent nonlinear loop is introduced (P3-PHYS-003's own explicit
  // "reuse the existing outer nonlinear iteration" constraint). Passing a
  // cfd::physics::ConstantProperty for both models reproduces solve()
  // above's result exactly, since evaluatePropertyField then returns a
  // uniform field equal to that constant everywhere and the field-based
  // EnergyEquation overloads are themselves exact-equivalent to the scalar
  // ones for a uniform field (P3-PHYS-003 section 4).
  //
  // rho only ever enters this equation via massFlux (already rho-weighted,
  // same reasoning as solve() above) -- a temperature-dependent density is
  // therefore never part of this overload's own signature; model rho(T)
  // instead via the same TemperatureProperty classes fed into
  // ThermalProperties::thermalDiffusivity(density) directly where that
  // quantity (not this transport equation) is what actually consumes it
  // (P3-PHYS-003 section 11's own density scope).
  [[nodiscard]] ThermalResult solve(
      const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& initialTemperature,
      const cfd::fields::SurfaceField& massFlux,
      const cfd::physics::TemperatureProperty& conductivityModel,
      const cfd::physics::TemperatureProperty& specificHeatModel,
      const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
      Real volumetricHeatSource = 0.0) const;

  // P2-THERMAL-005: the conjugate-heat-transfer counterpart of solve() --
  // assembles thermal::assembleConjugateConductionEquation (region-aware
  // conduction, no convection -- see ThermalInterface.hpp) instead of the
  // single-material convection-diffusion equation, but runs through the
  // exact same outer Picard loop and reports the exact same ThermalResult/
  // ThermalStatus shape, for the exact same reason (Adiabatic/HeatFlux
  // boundary lag -- see ThermalSolverSettings's own comment).
  [[nodiscard]] ThermalResult solveConjugateConduction(
      const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& initialTemperature,
      const ThermalRegionMap& regions,
      const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
      Real volumetricHeatSource = 0.0) const;

  [[nodiscard]] const ThermalSolverSettings& settings() const noexcept;

 private:
  ThermalSolverSettings settings_;
};

}  // namespace cfd::thermal
