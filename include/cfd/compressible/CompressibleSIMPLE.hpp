#pragma once

#include <optional>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::compressible {

// P12-COMP-002: every numerical control CompressibleSIMPLE needs, mirroring
// cfd::pressure_velocity::SIMPLESettings's own structure and precedent (a
// separate struct, not an edit to SIMPLESettings itself -- SIMPLE.cpp's
// own ~1300 passing tests ride on its exact current behavior, and this
// project's established pattern for a new physics module is a parallel
// type, never an edit to an existing one it must not disturb).
//
// pseudoTimeStep is a numerical stabilization parameter, not a physical
// time step -- CompressibleSIMPLE is a steady solver, like plain SIMPLE;
// see CompressibleRelaxedMomentum.hpp's own header comment for why a
// pseudo-transient (dual-time) framing is used to make
// CompressibleMomentum's existing transient-term machinery reusable
// inside a steady outer iteration. Defaults are reasonable starting
// points, not correctness guarantees, exactly like SIMPLESettings's own.
struct CompressibleSIMPLESettings {
  Index maxIterations{1000};

  Real velocityRelaxation{0.7};
  Real pressureRelaxation{0.3};
  Real pseudoTimeStep{1.0};

  Real velocityTolerance{1e-8};
  Real pressureTolerance{1e-8};
  Real continuityTolerance{1e-8};

  cfd::algebra::LinearSolverSettings momentumSolver;
  cfd::algebra::LinearSolverSettings pressureSolver;
};

// Throws InvalidArgumentError if maxIterations == 0; velocityRelaxation
// or pressureRelaxation is not finite or not in (0, 1]; pseudoTimeStep is
// not finite or <= 0; or any tolerance is not finite and > 0. Same
// validation style as validateSIMPLESettings.
void validateCompressibleSIMPLESettings(const CompressibleSIMPLESettings& settings);

// Mirrors cfd::pressure_velocity::SIMPLEStatus exactly (no turbulence-
// model case -- CompressibleSIMPLE does not accept one; see this class's
// own header comment on scope).
enum class CompressibleSIMPLEStatus {
  Converged,
  MaxIterations,
  MomentumFailure,
  PressureCorrectionFailure,
  NonFiniteState,
  InvalidConfiguration,
};

// Mirrors cfd::pressure_velocity::SIMPLEResult, with `density` added as
// genuinely iterated state (P12-COMP-002's own core requirement: updated
// via the EOS from the corrected pressure every outer iteration, not a
// post-hoc read after the loop the way the pre-existing
// ProjectRunner.cpp post-hoc pass works). `pressure` is gauge pressure,
// same convention as SIMPLEResult's own.
struct CompressibleSIMPLEResult {
  cfd::fields::VectorField velocity;
  cfd::fields::ScalarField pressure;
  cfd::fields::ScalarField density;
  cfd::fields::SurfaceField massFlux;

  CompressibleSIMPLEStatus status{CompressibleSIMPLEStatus::MaxIterations};
  Index iterations{0};

  Real finalUResidual{};
  Real finalVResidual{};
  Real finalPressureResidual{};
  Real finalContinuityResidual{};
  Real globalMassImbalance{};

  std::vector<Real> uResidualHistory;
  std::vector<Real> vResidualHistory;
  std::vector<Real> pressureResidualHistory;
  std::vector<Real> continuityHistory;

  [[nodiscard]] bool converged() const noexcept {
    return status == CompressibleSIMPLEStatus::Converged;
  }
};

// P12-COMP-002: a genuinely coupled compressible pressure-velocity
// solver -- a dedicated class, not a modification of
// cfd::pressure_velocity::SIMPLE (which stays exactly as it is, used
// unchanged for every incompressible/post-hoc-compressible case). Mirrors
// SIMPLE::solve()'s own outer-iteration structure exactly (momentum
// predict -> predictor flux -> response coefficients -> pressure
// correction -> correct velocity/pressure/flux -> residual check ->
// repeat), reusing this codebase's existing, unmodified building blocks
// wherever they apply unchanged: `cfd::pressure_velocity::
// computeMomentumResponseCoefficient`, `correctVelocity`,
// `correctFaceMassFlux` (all density-agnostic already) plus this module's
// own `assembleRelaxedCompressibleMomentumComponent`,
// `assembleCompressiblePressureCorrection`, and
// `evaluateCompressibleFaceDensity`/`calculateCompressibleMassFlux`
// (P12-COMP-001, already boundary-density-aware).
//
// Scope, matching this task's explicit exclusions: temperature is a
// fixed, one-way input for the whole solve() call (a constant for
// isothermal cases, or an already-converged external field for
// thermal-coupled cases) -- exactly the existing post-hoc pass's own
// convention, never updated by this solver itself (no energy-equation
// coupling, no viscous dissipation/pressure-work feedback -- "advanced
// compressible-energy formulation" is explicitly out of scope for
// P12-COMP-002). CPU-only (no GPU dispatch attempted here).
class CompressibleSIMPLE final {
 public:
  // referencePressure converts this solve's gauge pressure to absolute
  // for the EOS (same convention as CompressibleSetup/ProjectRunner's
  // existing post-hoc pass). referenceCell is the pressure-correction
  // null-space gauge, same role as SIMPLE's own (fixed at construction,
  // never moves between iterations -- determinism).
  explicit CompressibleSIMPLE(CompressibleSIMPLESettings settings,
                              ThermodynamicProperties thermodynamics, Real referencePressure,
                              Index referenceCell = 0);

  // `temperature` is fixed for this entire solve() call (see class-level
  // scope comment); `temperatureBoundaries` is null for an isothermal
  // case (temperature is then spatially uniform, exactly as
  // ProjectRunner's own existing isothermal-mode convention) or non-null
  // for a thermal-coupled case (temperature is the thermal block's own
  // already-converged field, and temperatureBoundaries lets boundary
  // faces evaluate density correctly, same as P12-COMP-001). Throws
  // nothing itself -- configuration/numerical failures are reported via
  // CompressibleSIMPLEResult::status, mirroring SIMPLE::solve()'s own
  // contract.
  [[nodiscard]] CompressibleSIMPLEResult solve(
      const cfd::mesh::Mesh& mesh, Real dynamicViscosity,
      const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
      const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
      const cfd::fields::ScalarField& temperature,
      const cfd::boundary::BoundaryConditionSet* temperatureBoundaries,
      cfd::fields::VectorField initialVelocity, cfd::fields::ScalarField initialPressure,
      cfd::fields::ScalarField initialDensity) const;

  [[nodiscard]] const CompressibleSIMPLESettings& settings() const noexcept;
  [[nodiscard]] Index referenceCell() const noexcept;

 private:
  CompressibleSIMPLESettings settings_;
  ThermodynamicProperties thermodynamics_;
  Real referencePressure_;
  Index referenceCell_;
};

}  // namespace cfd::compressible
