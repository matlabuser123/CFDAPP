#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/pressure_velocity/PressureVelocitySolver.hpp"
#include "cfd/pressure_velocity/SIMPLEProgress.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

namespace cfd::pressure_velocity {

// Steady, incompressible, constant-property SIMPLE (TODO.md P0 --
// SIMPLE). Orchestrates the already-verified physics/algebra/
// discretization layers (momentum assembly + relaxation, pressure
// correction, velocity/flux correction) rather than reimplementing any
// of them -- see this phase's other headers for the individual pieces
// and their own derivations/invariants.
class SIMPLE final : public PressureVelocitySolver {
 public:
  // referenceCell is the pressure-correction null-space gauge (TODO.md
  // section 23-24): fixed at construction, so it never moves between
  // iterations or between repeated solves of the same problem
  // (determinism -- section 69-70).
  //
  // turbulenceModel (P2-TURB-003) is an optional, non-owning pointer to
  // the cfd::turbulence::TurbulenceModel this solve() should query for
  // mu_eff each outer iteration (correct()ed against that iteration's
  // current velocity/pressure before momentum is assembled, the same
  // lagged-state convention every other current-state-dependent quantity
  // in this loop already follows). Left null (the default), solve()
  // constructs and uses its own local cfd::turbulence::LaminarModel
  // internally -- mu_t = 0 everywhere, every solve() call -- so every
  // existing two-argument SIMPLE(settings, referenceCell) call site
  // keeps its exact prior laminar behavior unchanged. Not owned; the
  // caller must keep a non-null model alive for the lifetime of this
  // object (mirrors PISO's own non-owned mesh_/fluid_/boundary
  // reference-member convention).
  //
  // temperature/buoyancy (P3-PHYS-001) are an optional, non-owning pair
  // (both null, or both non-null) for one-way Boussinesq coupling:
  // temperature -> buoyancy source -> momentum, held *fixed* across this
  // entire solve() call (the same "one-way coupling only" precedent
  // P2-THERMAL-004/005 already established for velocity -> temperature,
  // just in the opposite direction) -- solve() does not itself run a
  // thermal solve or update `temperature` from the velocity it computes.
  // A caller wanting full two-way natural-convection coupling composes
  // that by alternating SIMPLE::solve() (temperature fixed, velocity
  // updated) and cfd::thermal::ThermalSolver::solve() (velocity's mass
  // flux fixed, temperature updated) in its own outer loop -- the same
  // "CaseBuilder builds data, the caller builds/orchestrates the solve"
  // split this project already uses for thermal and turbulence, not a
  // new architecture invented here (TODO.md P3-PHYS-001's own Phase 4
  // diagram). Left null (the default, both), every existing
  // two/three-argument SIMPLE(...) call site keeps its exact prior
  // behavior -- structurally, not just numerically (see
  // assembleRelaxedMomentumComponent's own header comment). Not owned;
  // the caller must keep both alive for the lifetime of this solve()
  // call (same convention as turbulenceModel above).
  // progressCallback/cancellationCheck (P5-B): both default-empty, and
  // an empty std::function is never invoked -- every pre-P5 call site
  // (all with the prior 5-argument constructor still valid, unchanged)
  // keeps its exact prior behavior. See SIMPLEProgress.hpp's own header
  // comment for their contract.
  explicit SIMPLE(SIMPLESettings settings, Index referenceCell = 0,
                  cfd::turbulence::TurbulenceModel* turbulenceModel = nullptr,
                  const cfd::fields::ScalarField* temperature = nullptr,
                  const cfd::physics::BoussinesqBuoyancy* buoyancy = nullptr,
                  SIMPLEProgressCallback progressCallback = nullptr,
                  SIMPLECancellationCheck cancellationCheck = nullptr);

  [[nodiscard]] SIMPLEResult solve(const cfd::mesh::Mesh& mesh,
                                   const cfd::physics::FluidProperties& fluid,
                                   const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                   const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
                                   cfd::fields::VectorField initialVelocity,
                                   cfd::fields::ScalarField initialPressure) const override;

  [[nodiscard]] const SIMPLESettings& settings() const noexcept;
  [[nodiscard]] Index referenceCell() const noexcept;

 private:
  SIMPLESettings settings_;
  Index referenceCell_;
  cfd::turbulence::TurbulenceModel* turbulenceModel_;
  const cfd::fields::ScalarField* temperature_;
  const cfd::physics::BoussinesqBuoyancy* buoyancy_;
  SIMPLEProgressCallback progressCallback_;
  SIMPLECancellationCheck cancellationCheck_;
};

}  // namespace cfd::pressure_velocity
