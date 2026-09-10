#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::multiphase {

// P3-PHYS-005: conservative transport of the phase-1 volume fraction
// alpha:
//   d/dt (alpha*V_P) + sum_faces(massFlux_f * alpha_upwind,f) = 0
// -- pure advection, deliberately with **no** diffusion term (section 12's
// own explicit prohibition: alpha is not a species concentration, adding
// D*Laplacian(alpha) here would be a real physics bug, not a stylistic
// choice). This is unlike every other transport equation in this
// codebase (physics::MomentumEquation, thermal::EnergyEquation,
// species::SpeciesEquation), all of which combine a convection AND a
// diffusion contribution -- alpha transport intentionally has only the
// former.
//
// This is also the first transport equation in this codebase assembled
// as inherently *transient* rather than steady (see this header's own
// combiner below): a stationary interface under a Dirichlet boundary
// would just be a boring uniform field, but this task's own validation
// requirements (translating a phase slug through the domain, preserving
// a uniform field under a divergence-free flow) are fundamentally about
// evolving alpha through time, not converging it to a steady state --
// same distinction pressure_velocity::TransientMomentum.hpp's own
// implicit-Euler predictor draws from the steady
// physics::assembleMomentum it is built from.
//
// The convection contribution below is architecturally identical to
// physics::assembleConvectionContribution/
// species::assembleSpeciesConvectionContribution (coefficient 1, upwind,
// canonical massFlux) -- kept as its own independent implementation
// rather than shared code, the same "each physics domain owns its own
// otherwise-identical contribution assembler" convention this codebase
// already has for momentum/energy/species (see
// species::SpeciesSolver.cpp's own header comment on why no shared
// helper is factored out across domains).
struct VolumeFractionAssembly {
  cfd::algebra::LinearSystem system;
  cfd::algebra::Vector diagonal;
};

// sum_faces(massFlux_f * alpha_upwind,f) convection contribution, no
// diffusion. massFlux must already be the canonical face flux (this
// task's own section 11: "Use the existing canonical face flux from
// pressure-velocity coupling. Do not reconstruct another velocity field
// for interface transport."). Throws InvalidArgumentError if
// alpha.size() != mesh.numberOfCells() or massFlux.size() !=
// mesh.numberOfFaces().
void assembleVolumeFractionConvectionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::ScalarField& alpha,
    const cfd::boundary::BoundaryConditionSet& alphaBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// One implicit-Euler time step of the full transient volume-fraction
// equation: (alpha^{n+1} - alpha^n)*V_P/dt + convection(alpha^{n+1}) = 0.
// Reuses cfd::discretization::implicitEulerTimeDerivative directly for
// the time term (same building block
// pressure_velocity::assembleTransientMomentumComponent already uses for
// momentum's own transient predictor), with a bare coefficient of 1 (not
// a physical density -- alpha's own conservation law,
// d/dt(alpha)+div(u*alpha)=0, has no density multiplying the
// accumulation term; see this header's own governing-equation comment).
// `alphaOld` is both the previous-time-level field the time derivative
// needs AND the field boundary conditions are evaluated against (a
// Neumann-style FixedGradient boundary is therefore lagged by one
// timestep, exactly like pressure_velocity::TransientMomentum's own
// boundary evaluation against the *previous* velocity -- no inner Picard
// loop per timestep, unlike this codebase's steady-only
// thermal::ThermalSolver/species::SpeciesSolver, which need one for
// exactly the reason this equation's own transient nature already
// avoids: the lag is one timestep, not indefinite).
//
// Throws InvalidArgumentError if alphaOld.size() != mesh.numberOfCells(),
// massFlux.size() != mesh.numberOfFaces(), or dt is not finite or <= 0.
// Throws NumericalError if the assembled system contains a non-finite
// value.
[[nodiscard]] VolumeFractionAssembly assembleVolumeFractionTransportEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& alphaOld,
    const cfd::fields::SurfaceField& massFlux,
    const cfd::boundary::BoundaryConditionSet& alphaBoundaries, Real dt);

// Reporting-only boundedness diagnostic (section 14): {min, max} over
// `alpha`. Never clips -- same "report, do not silently enforce"
// convention as species::concentrationBounds.
struct AlphaBounds {
  Real minimum;
  Real maximum;
};
[[nodiscard]] AlphaBounds volumeFractionBounds(const cfd::fields::ScalarField& alpha);

// Phase-1 volume: sum_cells(alpha_P * V_P) (section 15/32). Phase-2
// volume is domainVolume - phaseVolume(...) (section 32's own identity),
// not a separate function -- computing it from (1-alpha) directly would
// just restate the same sum with no new information.
[[nodiscard]] Real phaseVolume(const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& alpha);

}  // namespace cfd::multiphase
