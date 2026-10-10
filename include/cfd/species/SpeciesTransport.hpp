#pragma once

#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/transport/ScalarTransport.hpp"

// P18.2 -- conservative multi-species transport.
//
//     d(rho Y_i)/dt + div(rho U Y_i) = -div(J_i) + omega_i        omega_i = 0 in P18.2
//
// WHAT THIS IS NOT: a third scalar-transport stack. Every per-species term is
// assembled by cfd::transport (P14) -- its transient, its upwind convection on
// the authoritative mass flux, its variable-diffusivity diffusion, its Patankar
// source. There is no SpeciesConvectionAssembler and no
// SpeciesDiffusionAssembler here, deliberately.
//
// WHAT IS GENUINELY NEW is the one thing no per-scalar assembler can express:
// the CROSS-SPECIES diffusive closure. Independent Fick fluxes
// J_i^0 = -rho D_i grad(Y_i) do not sum to zero when the D_i differ, and a
// non-zero sum_i J_i is net mixture mass created by diffusion. The correction
//
//     J_i = J_i^0 - Y_i sum_j J_j^0
//
// gives sum_i J_i = (sum_i J_i^0)(1 - sum_i Y_i) = 0 identically, because
// P18.1's MassFractionState guarantees sum_i Y_i = 1. Applied at FACES, so the
// closure holds per face at every iteration by construction rather than in the
// convergence limit. Full derivation:
// results/p18/species_transport/diffusion_closure.md
//
// THE DIFFUSIVITY PASSED IN IS Gamma_i = rho * D_i, not D_i. P14's diffusion
// term is Gamma_f |S_f| / d acting on the solved variable, which here is Y_i,
// and J_i = -rho D_i grad(Y_i). Units: D_i [m^2/s], rho [kg/m^3],
// Gamma_i [kg/(m s)], J_i [kg/(m^2 s)]. Passing Gamma as a per-cell field is
// what lets a variable density and a per-species D_i both work with no new
// machinery -- and it is what the pre-P18 species stack already does
// (fluid.density() * species.diffusivity()), so the constant-density,
// constant-D, single-species steady case reproduces it.
//
// omega_i IS ZERO IN P18.2, and it is an EXPLICIT zero rather than a default:
// `SpeciesSources` below is default-constructed to zero fields of the right
// size, so a caller that supplies nothing gets a source that is verifiably 0
// rather than an uninitialised state with ambiguous semantics (authorization
// section 14). No reaction rate, no stoichiometry, no Arrhenius -- those are
// P18.4-P18.6.
namespace cfd::species {

// Per-species Gamma_i = rho * D_i, one field per species, in species-index
// order. A separate type so a caller cannot accidentally pass bare D_i: the
// factory below is the only blessed way to build it from D_i and rho.
struct SpeciesDiffusivities {
  std::vector<cfd::fields::ScalarField> gamma;  // Gamma_i = rho * D_i  [kg/(m s)]
};

// Gamma_i = rho * D_i from a per-cell density and one constant D_i per species.
// Throws InvalidArgumentError on a size mismatch, a non-finite or negative D_i,
// or a non-finite or non-positive rho. D_i == 0 is REJECTED here, unlike
// SpeciesProperties which allows it for pure advection: P14's diffusion
// assembler requires a strictly positive diffusivity in every cell, so a zero
// would throw deeper with a less useful message. Pure advection is expressed by
// omitting the diffusion term, not by a zero Gamma -- see
// speciesTransportAssembly's `diffusion` flag.
[[nodiscard]] SpeciesDiffusivities makeSpeciesDiffusivities(const cfd::fields::ScalarField& density,
                                                            const std::vector<Real>& diffusivities);

// Per-species Patankar source. Zero-initialised: P18.2's authoritative
// omega_i = 0 is this type default-constructed for N species over N cells.
struct SpeciesSources {
  std::vector<cfd::transport::ScalarSourceFields> perSpecies;
};

// N species x N cells of exactly zero Su and Sp. The explicit zero of
// authorization section 14.
[[nodiscard]] SpeciesSources zeroSpeciesSources(Index speciesCount, Index cellCount);

// Old-time state for the conservative transient term. Both density levels are
// required and separately named, for the reason P14's TransportTimeState states:
//
//     a_P = rho_P^{n+1} V_P / dt        NEW density, diagonal
//     b_P = rho_P^n Y_{i,P}^n V_P / dt  OLD density, right-hand side
//
// rho^{n+1} Y^n on the right-hand side is NOT conservative when the density
// changes, and is the single easiest mistake to make here. Writing it requires
// actively naming the wrong pointer.
struct SpeciesTimeState {
  bool transient{false};
  Real dt{0.0};
  // The old time level, as raw fields for the same reason the entry points take
  // them: it is read, never validated here, and the step boundary that produced
  // it already validated it.
  const std::vector<cfd::fields::ScalarField>* fieldsOld{nullptr};
  const cfd::fields::ScalarField* rhoOld{nullptr};
  const cfd::fields::ScalarField* rhoNew{nullptr};
};

// The corrected diffusive face fluxes of a whole mixture, one SurfaceField per
// species in species-index order, plus the raw uncorrected fluxes and the
// per-face correction sum they were built from -- all three kept because
// section 6 asks for the raw flux, the correction and the proof to be reported
// separately rather than folded into one number.
//
// Sign: each value is the diffusive mass flow INTO THE OWNER across that face,
// [kg/s]. Boundary faces carry the owner's own contribution.
struct SpeciesDiffusiveFluxes {
  std::vector<cfd::fields::SurfaceField> raw;        // J^0_{i,f}
  std::vector<cfd::fields::SurfaceField> corrected;  // J_{i,f}
  cfd::fields::SurfaceField rawSum;                  // Sigma_f = sum_j J^0_{j,f}
};

// The corrected fluxes for the given state. This is the closure, and it is
// exposed independently of assembly so a test can measure max_f |sum_i J_{i,f}|
// directly (section 30) rather than inferring it from a solved field.
//
// `boundaries` is one BoundaryConditionSet per species, in species-index order.
// Throws InvalidArgumentError on any size or count mismatch.
// P18.2: takes the RAW FIELDS, not a MassFractionState, because a deferred-
// correction iterate legitimately violates sum(Y_i) = 1 until it converges and
// MassFractionState correctly refuses such a composition. The invariant is
// enforced at step boundaries, where it must hold; an iterate is a working
// vector. This was found the hard way -- see
// results/p18/species_transport/conservation.md finding F2.
[[nodiscard]] SpeciesDiffusiveFluxes speciesDiffusiveFaceFluxes(
    const cfd::mesh::Mesh& mesh, const SpeciesRegistry& registry,
    const std::vector<cfd::fields::ScalarField>& fields, const SpeciesDiffusivities& diffusivities,
    const std::vector<const cfd::boundary::BoundaryConditionSet*>& boundaries);

// Convenience overload for a validated state (a step boundary).
[[nodiscard]] SpeciesDiffusiveFluxes speciesDiffusiveFaceFluxes(
    const cfd::mesh::Mesh& mesh, const MassFractionState& state,
    const SpeciesDiffusivities& diffusivities,
    const std::vector<const cfd::boundary::BoundaryConditionSet*>& boundaries);

// max_f |sum_i J_{i,f}| and the largest raw flux it is judged against -- the two
// numbers section 30's criterion is stated on. Reported as a pair so the
// relative bound is computed from measured quantities, never from a constant.
struct ClosureResidual {
  Real maxAbsoluteSum{};  // max_f |sum_i J_{i,f}|
  Real maxAbsoluteRaw{};  // max_f max_i |J^0_{i,f}|
  Index worstFace{};
};

[[nodiscard]] ClosureResidual closureResidual(const SpeciesDiffusiveFluxes& fluxes);

// The frozen section-30 bound: 8 * eps * N_species * max_f max_i |J^0_{i,f}|.
// A RELATIVE bound scaled by the largest raw flux actually present, because an
// absolute tolerance on a dimensional flux is meaningless across meshes and
// diffusivities. Derived in diffusion_closure.md section 7, before measurement.
[[nodiscard]] Real closureTolerance(Index speciesCount, Real maxAbsoluteRaw) noexcept;

// Options for one transported species' assembly.
struct SpeciesTransportOptions {
  bool convection{true};
  bool diffusion{true};
  // Exact by default here, unlike P14 -- species is the path P18.2 owns, G4(c)
  // requires <= 2 outer iterations on a pure-Neumann problem, and the lagged
  // treatment measured 335. Lagged remains selectable so the negative control
  // can reintroduce the defect without editing production source.
  cfd::transport::GradientBoundaryTreatment gradientBoundary{
      cfd::transport::GradientBoundaryTreatment::Exact};
  cfd::discretization::NonOrthogonalCorrectionOptions nonOrthogonal{};
  cfd::transport::FaceDiffusivityInterpolation interpolation{
      cfd::transport::FaceDiffusivityInterpolation::Arithmetic};
  // When false the cross-species correction is omitted and the raw independent
  // Fick fluxes are used. Exists ONLY so the N5 negative control can break the
  // closure without editing production source; production always leaves it on.
  bool closureCorrection{true};
};

// One species' assembled system, ready to solve for Y_i.
[[nodiscard]] cfd::transport::ScalarTransportAssembly speciesTransportAssembly(
    const cfd::mesh::Mesh& mesh, Index speciesIndex, const SpeciesRegistry& registry,
    const std::vector<cfd::fields::ScalarField>& fields, const cfd::fields::SurfaceField& massFlux,
    const SpeciesDiffusivities& diffusivities,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    const cfd::transport::ScalarSourceFields& source, const SpeciesTimeState& timeState,
    const SpeciesDiffusiveFluxes* closure, const SpeciesTransportOptions& options = {});

// Convenience overload for a validated state (a step boundary).
[[nodiscard]] cfd::transport::ScalarTransportAssembly speciesTransportAssembly(
    const cfd::mesh::Mesh& mesh, Index speciesIndex, const MassFractionState& state,
    const cfd::fields::SurfaceField& massFlux, const SpeciesDiffusivities& diffusivities,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    const cfd::transport::ScalarSourceFields& source, const SpeciesTimeState& timeState,
    const SpeciesDiffusiveFluxes* closure, const SpeciesTransportOptions& options = {});

}  // namespace cfd::species
