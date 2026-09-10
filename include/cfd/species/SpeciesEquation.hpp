#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/species/SpeciesProperties.hpp"

namespace cfd::species {

// P3-PHYS-004: passive, non-reacting species transport, assembled with
// the exact same contribution-level pattern
// `cfd::thermal::assembleEnergyEquation` already establishes (itself
// modeled on `cfd::physics::MomentumEquation`) -- see this task's own
// section 2 instruction to use the energy equation as the closest
// architectural analogue. This is deliberately NOT a new numerical
// framework (section 2's own explicit prohibition): every face loop
// below is the same owner/neighbor/boundary structure, same
// `SparseMatrixBuilder`+RHS accumulation, same face-once assembly this
// codebase's every other transport equation already uses.
//
// Governing equation and the rho-placement decision (this task's own
// section 11, "inspect CFDApp's coefficient convention before
// implementation"): for constant density, this task's own section 1
// simplifies the conservative species equation
//   d(rho Y)/dt + div(rho U Y) = div(rho D grad(Y)) + S
// to
//   dY/dt + (U.grad)Y = div(D grad Y) + S/rho.
// This codebase's canonical face flux is `massFlux = rho*(U.Sf)`
// (physics::calculateMassFlux) -- section 7 *requires* convection reuse
// that exact canonical flux ("Do not reconstruct an unrelated velocity
// flux"), not a separate rho-free volume flux. Using massFlux directly
// (coefficient 1, exactly like MomentumEquation's own
// assembleConvectionContribution) therefore assembles div(rho U Y), not
// the rho-free div(U Y) the simplified equation above shows. For the
// diffusion term to stay physically consistent with that (rather than
// silently mixing a rho-weighted convection term with a rho-free
// diffusion term -- a real, dimensionally-inconsistent bug, not a
// stylistic choice), the diffusion coefficient must be rho*D, the
// density-weighted form this task's own section 11 explicitly permits
// as an alternative ("or the density-weighted equivalent if the
// existing conservative equation is expressed using rho*D"). The
// resulting assembled system is
//   div(rho U Y) - div(rho D grad Y) = S
// which is the original conservative equation's *left*-hand side exactly
// (rho constant, so this is the simplified equation multiplied through by
// the constant rho -- same solution Y, since A*Y=b and (rho*A)*Y=(rho*b)
// have identical solutions for rho != 0). rho itself comes from
// `cfd::physics::FluidProperties::density()`, the single source of truth
// already established for momentum/continuity -- never duplicated onto
// SpeciesProperties (same reasoning as ThermalProperties.hpp's own
// density-ownership comment).
//
// `concentration` is the current/lagged Y iterate, needed only to
// evaluate boundary conditions whose face value depends on the owner
// cell's state (a Neumann-style FixedGradient) -- same evaluate-at-
// current-state convention every other equation assembler in this
// codebase already uses.
//
// Species boundary conditions (this task's own section 13): no new BC
// classes are introduced. The generic `cfd::boundary::FixedValue`
// (Dirichlet -- e.g. an inlet's known inflow concentration) and
// `cfd::boundary::FixedGradient` (Neumann -- `FixedGradient(0.0)` for a
// zero-diffusive-flux impermeable wall or a "let it leave, don't force a
// value" outflow) already fully express what a species boundary needs;
// `assembleSpeciesDiffusionContribution`/`assembleSpeciesConvectionContribution`
// below have no special-case logic for any particular BC subtype, only
// ever calling the polymorphic `ScalarBoundaryCondition::boundaryValue`
// (same "no special-casing" property EnergyEquation.hpp's own header
// comment documents for FixedTemperature/HeatFlux/Adiabatic).

struct SpeciesAssembly {
  cfd::algebra::LinearSystem system;
  cfd::algebra::Vector diagonal;
};

// rho*D*Af/d diffusion contribution -- symmetric internal-face
// contribution (equal/opposite to owner and neighbor rows), Dirichlet-
// style boundary contribution added to the RHS. `diffusionCoefficient`
// is the already-resolved rho*D product (see this header's own governing-
// equation comment) -- kept as a bare Real, not a FluidProperties+
// SpeciesProperties pair, the same "caller has already resolved the
// physical coefficient" convention EnergyEquation.hpp's own
// `conductivity`/`specificHeat` parameters use, so this contribution
// stays independently testable with a hand-picked number. Throws
// InvalidArgumentError if concentration.size() != mesh.numberOfCells().
void assembleSpeciesDiffusionContribution(
    const cfd::mesh::Mesh& mesh, Real diffusionCoefficient,
    const cfd::fields::ScalarField& concentration,
    const cfd::boundary::BoundaryConditionSet& concentrationBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// Upwind-face-mass-flux convection contribution, coefficient 1 (Y is the
// transported quantity directly -- unlike EnergyEquation's convection,
// which scales massFlux by specificHeat since the transported quantity
// there is cp*T, not T itself). massFlux must already carry rho (see
// physics::calculateMassFlux) -- exactly the canonical flux this task's
// own section 7 requires reuse of. Throws InvalidArgumentError if
// concentration.size() != mesh.numberOfCells() or massFlux.size() !=
// mesh.numberOfFaces().
void assembleSpeciesConvectionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::ScalarField& concentration,
    const cfd::boundary::BoundaryConditionSet& concentrationBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// S*V uniform volumetric species source contribution, added to the RHS
// only. Throws InvalidArgumentError if volumetricSource is not finite.
void assembleSpeciesSourceContribution(const cfd::mesh::Mesh& mesh, Real volumetricSource,
                                       cfd::algebra::Vector& rhs);

// Combines the three contributions above into the full steady species
// transport equation (see this header's own governing-equation comment
// for the exact form assembled):
//   div(rho U Y) - div(rho D grad Y) = S
// `fluid` supplies rho (already baked into `massFlux`, and multiplied
// into the diffusion coefficient here); `species` supplies D. There is
// deliberately no transient (d(rho Y)/dt) term here -- standalone steady
// spatial assembly only, the same scope boundary
// `cfd::thermal::assembleEnergyEquation` itself already draws for the
// energy equation in this codebase (see EnergyEquation.hpp's own header
// comment) and this task's own section 21 explicitly permits skipping
// ("do not claim transient analytical validation unless one is actually
// performed" -- see TODO.md's own P3-PHYS-004 status note for the
// disclosed scope decision this task makes here).
//
// Throws InvalidArgumentError if concentration.size() !=
// mesh.numberOfCells(), massFlux.size() != mesh.numberOfFaces(), or
// volumetricSource is not finite. Throws NumericalError if the assembled
// system (matrix or RHS) contains a non-finite value.
[[nodiscard]] SpeciesAssembly assembleSpeciesTransportEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& concentration,
    const cfd::fields::SurfaceField& massFlux, const cfd::physics::FluidProperties& fluid,
    const SpeciesProperties& species,
    const cfd::boundary::BoundaryConditionSet& concentrationBoundaries,
    Real volumetricSource = 0.0);

// Reporting-only boundedness diagnostic (this task's own section 17):
// returns {min, max} over `concentration`. Never clips or modifies the
// field -- boundedness violation is a numerical-quality signal a caller
// reports, not something silently patched over ("If clipping is ever
// introduced, it must be an explicit documented numerical policy...").
struct ConcentrationBounds {
  Real minimum;
  Real maximum;
};
[[nodiscard]] ConcentrationBounds concentrationBounds(
    const cfd::fields::ScalarField& concentration);

}  // namespace cfd::species
