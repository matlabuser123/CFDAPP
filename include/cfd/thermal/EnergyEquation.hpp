#pragma once

#include <optional>
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
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

// The diffusion contribution of one boundary face to its owner's row, for
// a face whose two-point coefficient is Df: the conduction heat flow out
// of the owner through the face is Df (T_P - T_b).
//   - A value-prescribing condition (FixedTemperature, FixedValue:
//     cfd::discretization::prescribesBoundaryValue) fixes T_b:
//     diagonal = Df, source = Df T_b.
//   - A gradient-type condition (Adiabatic, FixedGradient, HeatFlux --
//     every such boundaryValue() is ownerValue + g d) gives T_b = T_P + g d,
//     so the flow is -Df g d whatever T_P is: diagonal = 0, source =
//     Df * boundaryValue(0, d) = Df g d. The prescribed flux is assembled
//     exactly (zero for Adiabatic), with no dependence on any iterate.
// Add `diagonal` to A(P, P) and `source` to the RHS of the owner row.
//
// P12-MESH-003 fix: gradient-type faces previously used A(P, P) += Df,
// rhs += Df T_b with T_b evaluated from the PREVIOUS outer iterate, i.e. a
// wall flow Df (T_P^new - T_P^old - g d) in the solved system. The outer
// loop stopped on max |dT| < tolerance and returned a field whose own
// boundary values it had never been solved with, leaving up to
// Df * tolerance of spurious flow per Neumann face (and hundreds of outer
// iterations for a linear conduction problem). `temperature` is still
// needed for value-prescribing conditions that read the owner value.
struct BoundaryDiffusionContribution {
  Real diagonal{0.0};
  Real source{0.0};
};
// P12-DIFF-002: `boundaryValueCoefficient` is the multiplier of a PRESCRIBED
// boundary temperature on the RHS (FaceDiffusionTerms::boundaryValueCoefficient).
// It equals `coefficient` for the two-point form, and differs from it only when
// the second-order one-sided reconstruction is active. Gradient-type conditions
// are unaffected either way: their prescribed flux still uses `coefficient`
// exactly as before.
//
// Prefer assembleThermalBoundaryFaceContribution below over calling this
// directly: passing only `coefficient` silently selects the historical two-point
// wall flux, which is what made the conjugate path diverge from the
// single-material one (see that function's own comment).
[[nodiscard]] BoundaryDiffusionContribution boundaryDiffusionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
    const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries, Real coefficient,
    std::optional<Real> boundaryValueCoefficient = std::nullopt);

// The stored diagonal coefficient of `row`, or 0 when the matrix builder
// dropped it as an exact zero: a cell with no internal face, no
// prescribed-temperature face and no outflow (every face gradient-type, a
// one-cell mesh). Its equation is singular and the linear solve reports
// that through ThermalResult::status instead of the assembly throwing.
[[nodiscard]] Real storedDiagonalOrZero(const cfd::algebra::SparseMatrix& matrix, Index row);

// k*Af/d diffusion contribution: symmetric internal-face contribution
// (equal/opposite to owner and neighbor rows, same convention as
// MomentumEquation's assembleDiffusionContribution with k in place of
// mu), Dirichlet-style boundary contribution added to the RHS. Throws
// InvalidArgumentError if temperature.size() != mesh.numberOfCells().
//
// P12-NUM-003: `nonOrthogonal` (default disabled -- exactly the
// pre-existing operator) applies the shared non-orthogonal correction
// (cfd::discretization::internalFaceDiffusionTerms/
// boundaryFaceDiffusionTerms -- the one implementation, not a copy): every
// internal face, and every boundary face whose condition prescribes the
// temperature (FixedValue/FixedTemperature); HeatFlux/Adiabatic/
// FixedGradient faces are never corrected (prescribed flux). grad(T) is
// cfd::discretization::gradient(temperature, temperatureBoundaries,
// nonOrthogonal.gradientScheme), evaluated from the temperature passed in
// (lagged -- ThermalSolver's outer Picard loop converges it). On an
// orthogonal mesh the corrected assembly is bit-identical.
// P12-DIFF-002: the cell gradient the Dirichlet wall reconstruction needs for its tangential
// transfer term. Always built -- WHICH wall-flux scheme is used must not depend on whether the
// iterative non-orthogonal correction is enabled (see
// results/p12-diff-002/a2/activation_architecture.md).
[[nodiscard]] cfd::fields::VectorField thermalBoundaryCorrectionGradient(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::discretization::GradientScheme gradientScheme =
        cfd::discretization::GradientScheme::GreenGauss);

// P12-DIFF-002: the COMPLETE contribution of one boundary face to a thermal diffusion assembly --
// the second-order one-sided Dirichlet reconstruction where a valid inward stencil exists (owner
// coefficient, far-cell coefficient, prescribed-value coefficient and the explicit transfer term),
// the historical two-point fallback where none exists, and the exact prescribed flux for
// gradient-type conditions.
//
// This exists as ONE function because it must not be re-implemented per caller. It originally lived
// inline in assembleThermalDiffusionContribution; the region-aware conjugate assembly
// (ThermalInterface.cpp) had its own hard-coded `conductivity * face.area() / distance` copy, and
// when P12-DIFF-002 A2 made the single-material wall flux unconditionally second order the two
// paths silently diverged -- a single-region conjugate solve differed from the equivalent
// single-material solve by up to 3.02 K
// (results/p12-diff-002/thermal-interface-fix/acceptance_gate.md section 1). Both callers now share
// this implementation, so that divergence cannot recur.
//
// `face` must be a boundary face and `gradT` the field from
// thermalBoundaryCorrectionGradient above. Stencil availability is topological (MeshGeometry::
// boundaryInwardStencil); no floating-point geometry predicate decides it.
void assembleThermalBoundaryFaceContribution(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face, Real conductivity,
    const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    const cfd::fields::VectorField& gradT, cfd::algebra::SparseMatrixBuilder& builder,
    cfd::algebra::Vector& rhs);

void assembleThermalDiffusionContribution(
    const cfd::mesh::Mesh& mesh, Real conductivity, const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {});

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
// any conductivity value is non-finite or <= 0. `nonOrthogonal`: as for
// the scalar overload above.
void assembleThermalDiffusionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& conductivity,
    const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {});

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

// P12-NUM-006: the spatially varying counterpart -- one volumetric source
// value [W/m^3] per cell (the source at the cell centroid; midpoint-rule
// volume integral): rhs[P] += Q_P * V_P. Generic: nothing here knows what
// the field represents. Throws InvalidArgumentError if
// volumetricHeatSource.size() != mesh.numberOfCells() or any value is
// non-finite.
void assembleThermalSourceContribution(const cfd::mesh::Mesh& mesh,
                                       const cfd::fields::ScalarField& volumetricHeatSource,
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
    Real volumetricHeatSource = 0.0,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {});

// P12-NUM-006: same equation and assembly as the overload above, with a
// per-cell volumetric heat source (assembleThermalSourceContribution's
// field overload) in place of the uniform one. Throws as the overload
// above, plus InvalidArgumentError for a wrongly sized or non-finite
// source field.
[[nodiscard]] EnergyAssembly assembleEnergyEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature,
    const cfd::fields::SurfaceField& massFlux, const ThermalProperties& thermal,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries,
    const cfd::fields::ScalarField& volumetricHeatSource,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {});

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
    Real volumetricHeatSource = 0.0,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {});

}  // namespace cfd::thermal
