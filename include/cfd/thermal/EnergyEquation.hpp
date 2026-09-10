#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/thermal/ThermalProperties.hpp"

namespace cfd::thermal {

// Result of assembling the steady-state energy equation. The diagonal
// (aP per cell) is exposed directly, same reasoning as
// physics::MomentumAssembly -- a future thermal solver will likely need
// 1/aP-like quantities, and this saves every caller from rescanning the
// matrix (P2-THERMAL-002).
struct EnergyAssembly {
  cfd::algebra::LinearSystem system;
  cfd::algebra::Vector diagonal;
};

// Each assembleThermalXxxContribution below adds its term directly into a
// shared, caller-owned SparseMatrixBuilder + RHS Vector, exactly like
// physics::MomentumEquation's assembleDiffusionContribution/
// assembleConvectionContribution -- this keeps diffusion, convection, and
// the heat source independently testable without needing to disable a
// term via unphysical property values (k=0, cp=0 are rejected by
// ThermalProperties's own constructor). `temperature` is the current/
// lagged temperature iterate, needed only to evaluate boundary conditions
// whose face value depends on the owner cell's state (FixedGradient
// reconstructs its face value from the owner temperature; FixedValue
// ignores it) -- mirrors how MomentumEquation.cpp's boundaryVelocity()
// and discretization::interpolateFace already evaluate boundary
// conditions against a current field value.
//
// Temperature boundary conditions: `temperatureBoundaries` accepts any
// cfd::boundary::ScalarBoundaryCondition -- this function has no
// special-case logic for any particular BC class, it only ever calls the
// polymorphic boundaryValue(ownerValue, normalDistance). The
// thermal-specific classes (P2-THERMAL-003:
// cfd::boundary::FixedTemperature, cfd::boundary::HeatFlux,
// cfd::boundary::Adiabatic) are the intended real-world choice --
// FixedTemperature(T) for a prescribed wall/inlet temperature, HeatFlux
// for a physically-scaled q''=-k*dT/dn condition, Adiabatic for a
// perfectly insulated (zero-flux) wall. The generic
// cfd::boundary::FixedValue/FixedGradient primitives (already used for
// pressure) remain equally valid here too, since they implement the same
// interface -- FixedValue(T) is mathematically identical to
// FixedTemperature(T), and FixedGradient(0.0) to Adiabatic -- and several
// of this file's own unit tests still use them directly for that reason.

// k*Af/d diffusion contribution: symmetric internal-face contribution
// (equal/opposite to owner and neighbor rows, same convention as
// MomentumEquation's assembleDiffusionContribution with k in place of
// mu), Dirichlet-style boundary contribution added to the RHS. Throws
// InvalidArgumentError if temperature.size() != mesh.numberOfCells().
void assembleThermalDiffusionContribution(
    const cfd::mesh::Mesh& mesh, Real conductivity, const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// P3-PHYS-003: same physics as the constant-conductivity overload above,
// but with a per-cell conductivity field (k(T), typically produced by
// cfd::physics::evaluatePropertyField from a TemperatureProperty) instead
// of one scalar -- mirrors physics::MomentumEquation's field-based
// assembleDiffusionContribution(effectiveViscosity, ...) overload exactly,
// including its face-interpolation convention: internal faces use the
// project's established distance-weighted linear interpolation
// (cfd::discretization::interpolateInternalFace) to get k at the face; a
// boundary face has no neighbor to interpolate against, so it uses the
// owner cell's own conductivity directly, matching how the scalar overload
// already applies one value uniformly including at boundaries.
//
// The scalar overload above is intentionally left as a separate, untouched
// code path -- callers that only ever pass one constant conductivity keep
// their existing, exact floating-point behavior (P3-PHYS-003 section 4's
// backward-equivalence requirement). Throws InvalidArgumentError if
// temperature.size() or conductivity.size() != mesh.numberOfCells(), or if
// any conductivity value is non-finite or <= 0.
void assembleThermalDiffusionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& conductivity,
    const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// specificHeat * upwind-face-mass-flux convection contribution. massFlux
// must already carry rho (see physics::calculateMassFlux) -- exactly like
// MomentumEquation's convection contribution, which uses the raw mass
// flux directly because rho is already baked in, this multiplies by
// specificHeat only, since the transported quantity here is cp*T rather
// than bare velocity. Throws InvalidArgumentError if
// temperature.size() != mesh.numberOfCells() or
// massFlux.size() != mesh.numberOfFaces().
void assembleThermalConvectionContribution(
    const cfd::mesh::Mesh& mesh, Real specificHeat, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// P3-PHYS-003: same physics as the constant-specificHeat overload above,
// but with a per-cell specific-heat field (cp(T)) instead of one scalar.
// Unlike conductivity's diffusion contribution, cp is not face-interpolated
// here: the transported quantity at each face is the *upwind* cell's cp*T,
// so this evaluates cp at whichever cell (owner or neighbor) the upwind
// decision already selects for T itself -- the same cell the upwind mass
// flux is already picking, not a separate face-averaged value. This
// matches the convention that a transported scalar's coefficient travels
// with the fluid parcel it came from, not with the face it happens to be
// crossing (distinct from diffusion, which is an intrinsically two-cell,
// symmetric process and so is face-interpolated instead).
//
// The scalar overload above is intentionally left as a separate, untouched
// code path. Throws InvalidArgumentError if temperature.size() or
// specificHeat.size() != mesh.numberOfCells(), massFlux.size() !=
// mesh.numberOfFaces(), or any specificHeat value is non-finite or <= 0.
void assembleThermalConvectionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& specificHeat,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs);

// Q*V uniform volumetric heat source [W/m^3] contribution, added to the
// RHS only (Q is a fixed source, not a function of the unknown
// temperature, so it contributes no matrix entries). Throws
// InvalidArgumentError if volumetricHeatSource is not finite.
void assembleThermalSourceContribution(const cfd::mesh::Mesh& mesh, Real volumetricHeatSource,
                                       cfd::algebra::Vector& rhs);

// Combines the three contributions above into the full steady-state,
// constant-property energy equation:
//   rho*cp*(U . grad)T = k*Laplacian(T) + Q
// rearranged to match physics::MomentumEquation's governing-equation
// convention (convection - diffusion = source):
//   rho*cp*(U . grad)T - k*Laplacian(T) = Q
//
// massFlux must already carry rho (physics::calculateMassFlux); thermal
// supplies conductivity/specificHeat. There is deliberately no
// FluidProperties parameter here: density only ever enters this equation
// through massFlux (already rho-weighted), so an independent raw density
// argument would be a second, possibly-disagreeing source of truth for
// exactly the quantity physics::FluidProperties::density() already owns
// (see ThermalProperties.hpp's own density-ownership comment) -- this
// mirrors assembleConvectionContribution above and
// physics::assembleConvectionContribution, neither of which takes
// FluidProperties for the same reason.
//
// No transient term (rho*cp*V/deltaT) yet -- standalone steady spatial
// assembly only (P2-THERMAL-002); the implicit-Euler thermal transient
// term is a later task, deliberately not added here to keep this task's
// scope disciplined (mirrors how P0 -- Incompressible Physics verified
// momentum assembly before SIMPLE integration). No SIMPLE/PISO coupling
// either -- see TODO.md "P2 -- Thermal".
//
// Throws InvalidArgumentError if temperature.size() != mesh.numberOfCells(),
// massFlux.size() != mesh.numberOfFaces(), volumetricHeatSource is not
// finite, or a non-finite value reaches a *matrix* coefficient (e.g. a
// non-finite massFlux entry) -- algebra::SparseMatrix's own constructor
// already rejects those at build() time, before this function's own
// finiteness check runs. Throws NumericalError if a non-finite value
// reaches only the RHS (e.g. a non-finite temperature at a boundary whose
// condition depends on the owner value, such as FixedGradient) --
// algebra::Vector does not self-validate, so that case is caught by this
// function's own post-assembly check instead.
[[nodiscard]] EnergyAssembly assembleEnergyEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature,
    const cfd::fields::SurfaceField& massFlux, const ThermalProperties& thermal,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    Real volumetricHeatSource = 0.0);

// P3-PHYS-003: same combiner as above, but taking per-cell conductivity/
// specificHeat fields (typically produced once per outer Picard iteration
// by cfd::physics::evaluatePropertyField from a TemperatureProperty) in
// place of one constant ThermalProperties -- routes to the field-based
// assembleThermalDiffusionContribution/assembleThermalConvectionContribution
// overloads above instead of the scalar ones, otherwise identical (same
// governing equation, same source term, same finiteness checks). The
// scalar overload above is intentionally left as a separate, untouched
// code path -- passing a uniform field here reproduces its result exactly
// (P3-PHYS-003 section 4). Throws InvalidArgumentError if
// temperature/conductivity/specificHeat.size() != mesh.numberOfCells(),
// massFlux.size() != mesh.numberOfFaces(), volumetricHeatSource is not
// finite, or any conductivity/specificHeat value is non-finite or <= 0.
[[nodiscard]] EnergyAssembly assembleEnergyEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& conductivity,
    const cfd::fields::ScalarField& specificHeat,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    Real volumetricHeatSource = 0.0);

}  // namespace cfd::thermal
