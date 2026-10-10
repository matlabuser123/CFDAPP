#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"

// P14 -- Generic Transport Foundation.
//
// WHAT THIS IS, AND WHAT IT DELIBERATELY IS NOT
// ---------------------------------------------
// This is NOT a new finite-volume framework. The Phase A audit
// (results/p14/transport-foundation/audit.md) counted what already existed:
//
//   scalar convection assemblers already in the tree     4
//   scalar diffusion  assemblers already in the tree     5
//   scalar source     assemblers already in the tree     2
//
// and, decisively, found that a *generic* scalar transport assembler already
// existed -- `cfd::turbulence::assembleScalarTransportEquation` -- with
// variable diffusivity, mass-flux convection, Patankar Su/Sp sources and
// polymorphic scalar boundary conditions. Its own header describes it as
// "model-agnostic assembly, model-specific coefficients". It was simply
// unreachable: living in `cfd::turbulence`, it could not be used by thermal,
// species or multiphase without those modules depending on the turbulence
// model.
//
// So P14 PROMOTES that assembler rather than writing a tenth one. The loops
// below are the same code, in the same order, calling the same discretization
// primitives with the same operands.
//
// P14 MIGRATION, COMPLETE. This is now the ONLY scalar transport assembly in
// the codebase. All six production turbulence equations -- k-epsilon k and
// epsilon, k-omega k and omega, SST k and omega -- route their diffusion,
// convection and implicit source through the functions below, via
// `cfd::turbulence::solveRelaxedScalarTransport`. That wrapper keeps only what
// this layer deliberately does not do: Patankar under-relaxation, imposed cells
// applied with `fixRow` AFTER relaxation, the linear solve, the floor, and the
// re-assertion of imposed values over that floor. `cfd::transport` never sees
// an imposed cell and needs no imposed-cell semantics of its own.
//
// The duplicated `cfd::turbulence` diffusion and source loops that this
// assembler was promoted from have been DELETED. They were removed only after
// every production consumer had migrated with bitwise equivalence gated at each
// step, and only after their output had been frozen -- see
// tests/unit/transport/test_transport_characterization.cpp (matrix, sparsity
// and RHS, steady and non-orthogonal) and
// tests/unit/turbulence/test_p14_migration_equivalence.cpp (the solved field
// through the full wrapper). Those frozen values are the regression oracle that
// replaced the equivalence tests, which retired with the second implementation
// they compared against.
//
// Convection was never duplicated: both layers always called the same
// `cfd::thermal::assembleThermalConvectionContribution` with a unit specific
// heat.
//
// THE CONVENTION, STATED ONCE
// ---------------------------
//   a_P phi_P + sum_N a_N phi_N = b_P        (every assembler in this codebase)
//
//   convection   upwind on the owner-oriented face mass flux F_f, which
//                ALREADY CARRIES rho (physics::calculateMassFlux). The
//                transported quantity enters with coefficient 1.
//   diffusion    Gamma_f * |Sf| / d, with Gamma face-interpolated by
//                discretization::interpolateInternalFace's distance-weighted
//                convention ((dNf*gP + dPf*gN)/(dPf+dNf)) on internal faces
//                and taken as the OWNER cell's value on boundary faces.
//                Chosen because it is what the production variable-diffusivity
//                path already does, not by preference (P14 Phase B6).
//   source       Patankar S = Su + Sp*phi:  a_P -= Sp_P * V_P ; b_P += Su_P * V_P.
//   transient    conservative implicit Euler, see TransportTimeState below.
//
// Gamma and rho are consumed as ScalarFields, never through virtual calls
// inside the face loops (Phase B6: the assembly path stays data-oriented so it
// can be mirrored on the device).

namespace cfd::transport {

// The assembled system plus its diagonal, kept separately because callers
// (under-relaxation, residual normalisation) need a_P after the matrix has
// been built and moved from.
struct ScalarTransportAssembly {
  cfd::algebra::LinearSystem system;
  cfd::algebra::Vector diagonal;
};

// Old-time state for a transient solve. Steady when `transient` is false, in
// which case every other member is ignored and the assembled system is bitwise
// what the steady path produces.
//
// rhoOld and rhoNew are BOTH required, and that is the point (Phase B3): the
// conservative discretisation of d(rho phi)/dt is
//
//     a_P^time = rho_P^{n+1} V_P / dt          (diagonal, uses the NEW density)
//     b_P^time = rho_P^n phi_P^n V_P / dt      (RHS,      uses the OLD density)
//
// Using rho^{n+1} phi^n on the right-hand side is NOT conservative when the
// density changes, and is the single easiest mistake to make here. Carrying
// the two levels as separate, separately-named pointers is what makes writing
// that mistake require actively naming the wrong one.
struct TransportTimeState {
  bool transient{false};
  Real dt{0.0};
  const cfd::fields::ScalarField* phiOld{nullptr};
  const cfd::fields::ScalarField* rhoOld{nullptr};
  const cfd::fields::ScalarField* rhoNew{nullptr};
};

// Per-cell Patankar source, as fields rather than a virtual interface.
//
// The brief's illustrative API had a `ScalarSource` base class with a virtual
// evaluate() per cell. That is not used here, for a reason the audit makes
// concrete: the assembly path must be mirrorable on the device (Phase I), and
// a virtual call per cell cannot be. Callers that want a model -- a reaction
// rate, a turbulence production term -- evaluate it into these two fields
// above this layer, which is exactly what KEpsilonModel already does today.
//
// Sp <= 0 is the Patankar stability requirement. See the `requireNonPositiveSp`
// parameter on the assembler for how P14 handles it (Phase B5): validated and
// reported, never silently clamped.
struct ScalarSourceFields {
  cfd::fields::ScalarField Su;  // explicit part      -> b_P += Su_P * V_P
  cfd::fields::ScalarField Sp;  // implicit/linearised -> a_P -= Sp_P * V_P
};

// How Gamma is evaluated at an internal face.
//
// Arithmetic is the DEFAULT and is the convention the production
// variable-diffusivity path already uses, so it is what the bitwise
// equivalence gate against the legacy assembler exercises:
//
//   Gamma_f = (dNf*Gamma_P + dPf*Gamma_N) / (dPf + dNf)
//
// Harmonic is added for a physically distinct case the arithmetic form gets
// wrong: a DISCONTINUOUS diffusivity, where the correct face value is the one
// that preserves flux continuity across the interface rather than the average
// of the two sides. The general distance-weighted form is
//
//   Gamma_f = d_PN / (d_Pf/Gamma_P + d_fN/Gamma_N)
//
// which reduces to 2*Gamma_P*Gamma_N/(Gamma_P+Gamma_N) at a midpoint face.
// It matters for a conjugate interface (k jumping between solid and fluid) and
// for multiphase property jumps -- P15 and P19 respectively. Selecting it is
// deliberate and never automatic: the default keeps every existing result
// bit-for-bit.
enum class FaceDiffusivityInterpolation {
  Arithmetic,  // distance-weighted mean -- the existing production convention
  Harmonic,    // flux-preserving across a discontinuity
};

// How a GRADIENT (non-prescribed-value) boundary face is assembled.
//
// P18.2. For dphi/dn = g the boundary value is phi_b = phi_P + g d, so the
// diffusive flow into the owner is
//
//     Df (phi_b - phi_P) = Df (g d) = Gamma |S| g          Df = Gamma |S| / d
//
// which contains NO phi_P. The exact contribution is therefore a ZERO diagonal
// and a fully known right-hand side, which is what cfd::thermal's energy
// assembly has done since P12-MESH-003:
//
//     return {0.0, coefficient * boundaryValue(0.0, distance)};
//
// `Lagged` instead adds +Df to the diagonal and -Df phi_P^{old} to the RHS.
// Those cancel only at convergence, so the converged answer is the same but the
// cost is not: measured at 335 outer Picard iterations against thermal's 2 on an
// identical pure-Neumann problem (results/p18/audit/boundary_lag.md). It is a
// fixed-point iteration for a quantity that has a closed form.
//
// SO WHY IS Lagged STILL THE DEFAULT? Because this assembler is the ONLY scalar
// transport assembly in the codebase and all six production turbulence equations
// route through it. Changing their assembled matrices is not P18.2's business --
// P18.2 is authorized for species, and its authorization says not to fix
// unrelated boundary infrastructure. Defaulting to the existing behaviour keeps
// every current caller bitwise unchanged.
//
// This is the same pattern, for the same reason, as FaceDiffusivityInterpolation
// ("the default keeps every existing result bit-for-bit") and
// requireNonPositiveSp ("defaults to OFF, because turning it on by default would
// change the behaviour of the existing turbulence call sites, which is not P14's
// business"). Moving turbulence to Exact is a separate, separately-authorizable
// change needing its own frozen gate: see results/p18/species_transport/audit.md
// section 4, and TODO.md's technical debt.
enum class GradientBoundaryTreatment {
  Lagged,  // pre-P18.2 behaviour: non-zero diagonal + the previous iterate on the RHS
  Exact,   // zero diagonal, fully known RHS -- what thermal does
};

// Gamma at an internal face under the chosen convention. Exposed so a test can
// assert the two forms against hand calculations independently of assembly.
[[nodiscard]] Real faceDiffusivity(const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
                                   const cfd::fields::ScalarField& diffusivity,
                                   FaceDiffusivityInterpolation interpolation);

// Gamma*Af/d diffusion with a per-cell diffusivity field. Internal faces
// distance-weight `diffusivity` via discretization::interpolateInternalFace;
// boundary faces use the owner cell's own value (there is no neighbour to
// interpolate against). Rejects a mismatched size or a non-finite/non-positive
// entry.
void assembleScalarDiffusionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& diffusivity,
    const cfd::fields::ScalarField& phi, const cfd::boundary::BoundaryConditionSet& boundaries,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {},
    FaceDiffusivityInterpolation interpolation = FaceDiffusivityInterpolation::Arithmetic,
    GradientBoundaryTreatment gradientBoundary = GradientBoundaryTreatment::Lagged);

// Upwind convection on the owner-oriented face mass flux, coefficient 1.
void assembleScalarConvectionContribution(const cfd::mesh::Mesh& mesh,
                                          const cfd::fields::SurfaceField& massFlux,
                                          const cfd::fields::ScalarField& phi,
                                          const cfd::boundary::BoundaryConditionSet& boundaries,
                                          cfd::algebra::SparseMatrixBuilder& builder,
                                          cfd::algebra::Vector& rhs);

// Patankar implicit source: a_P -= Sp_P*V_P, b_P += Su_P*V_P.
//
// `requireNonPositiveSp` (Phase B5): the pre-existing turbulence path made
// Sp <= 0 a *caller* contract, enforced nowhere. A generic API reached by
// future callers needs the check somewhere it cannot be skipped, so it is
// available here -- and defaults to OFF, because turning it on by default
// would change the behaviour of the existing turbulence call sites, which is
// not P14's business. Never clamps: a positive Sp throws, it is not quietly
// turned into zero.
void applyImplicitScalarSource(const cfd::mesh::Mesh& mesh,
                               cfd::algebra::SparseMatrixBuilder& builder,
                               cfd::algebra::Vector& rhs, const cfd::fields::ScalarField& Su,
                               const cfd::fields::ScalarField& Sp,
                               bool requireNonPositiveSp = false);

// Conservative implicit-Euler transient contribution (see TransportTimeState).
// Adds rho^{n+1}V/dt to the diagonal and rho^n phi^n V/dt to the RHS. A steady
// `timeState` adds nothing at all -- not a zero-valued term, but no call.
void applyTransientContribution(const cfd::mesh::Mesh& mesh,
                                cfd::algebra::SparseMatrixBuilder& builder,
                                cfd::algebra::Vector& rhs, const TransportTimeState& timeState);

// The generic scalar transport equation.
//
// Assembly order is diffusion, then convection, then source, then transient --
// the first three exactly as the promoted turbulence path did them, because
// SparseMatrixBuilder accumulates and floating-point addition is not
// associative, so the order is part of the numerical result, not a detail.
//
// Throws InvalidArgumentError on any size mismatch or a non-finite/
// non-positive diffusivity; NumericalError if the assembled system is
// non-finite.
[[nodiscard]] ScalarTransportAssembly assembleScalarTransportEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& phi,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& diffusivity,
    const cfd::boundary::BoundaryConditionSet& boundaries, const cfd::fields::ScalarField& Su,
    const cfd::fields::ScalarField& Sp,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {},
    const TransportTimeState& timeState = {},
    FaceDiffusivityInterpolation interpolation = FaceDiffusivityInterpolation::Arithmetic,
    GradientBoundaryTreatment gradientBoundary = GradientBoundaryTreatment::Lagged);

}  // namespace cfd::transport
