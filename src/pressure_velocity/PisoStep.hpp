#pragma once

// P12-MESH-007 -- library-internal (not installed, not a public API): the one
// PISO time-step algorithm, shared by PISO (a static mesh) and AlePISO (a
// moving mesh) so that the pressure-velocity algorithm exists exactly once.
// See results/p12-mesh-007/architecture.md section 3.7.

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/solver/TransientSolver.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

namespace cfd::pressure_velocity::detail {

// The ALE terms of one step on a moving mesh (the mesh already holds the
// geometry at t^{n+1}): V^n per cell, and the convecting flux F^n - rho dV/dt
// (cfd::physics::relativeMassFlux) that replaces F^n in the momentum
// predictor's convection and in the CFL number. Both must outlive the call.
struct AleStepTerms {
  const cfd::fields::ScalarField* previousVolume{nullptr};
  const cfd::fields::SurfaceField* convectingMassFlux{nullptr};
};

// One PISO step -- exactly the algorithm documented on PISO::solveTimeStep.
// ale == nullptr is the static formulation (PISO); otherwise the momentum
// predictor is cfd::pressure_velocity::assembleAleTransientMomentumComponent
// with *ale, and the pre-step CFL is computed from ale->convectingMassFlux.
// The pressure corrections, continuity and every status are identical in
// both cases. A size mismatch of the ALE terms is InvalidConfiguration.
[[nodiscard]] cfd::solver::TransientStepResult solvePisoStep(
    const cfd::mesh::Mesh& mesh, const cfd::physics::FluidProperties& fluid,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries, const PISOSettings& settings,
    Index referenceCell, cfd::turbulence::TurbulenceModel* turbulenceModel,
    const cfd::solver::TransientState& previousState, Real dt, const AleStepTerms* ale);

}  // namespace cfd::pressure_velocity::detail
