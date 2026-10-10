#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

// P17.5 -- the CONSERVATIVE total-energy equation, in flux form.
//
// THE CONTINUUM EQUATION
// ----------------------
//   d(rho eTotal)/dt + div[(rho eTotal + p) U] = div(tau . U) - div(q) + rho f . U + S_E
//   q = -k grad(T)
//
// THE DISCRETE FORM ACTUALLY IMPLEMENTED, for cell P
// -------------------------------------------------
//   (rhoE_new - rhoE_old) V / dt
//     + sum_f  m_dot_f hTotal_f                 convective + PRESSURE work
//     + sum_f  q_f . n_f A_f                    conduction
//   =  sum_f (tau . U)_f . n_f A_f               viscous work
//     + rho f . U V                             body-force power
//     + S_E V                                   volumetric source
//
// SIGN CONVENTION, stated once and used everywhere
// -----------------------------------------------
// Every function named ...Divergence returns the NET OUTFLOW OUT OF THE CELL, INTEGRATED over the
// cell volume, in watts [W]. So a positive value means the cell is losing that much energy through
// that mechanism. The balance is then
//
//   accumulation + convectiveOutflow + conductiveOutflow
//     - viscousWorkInflow - bodyForcePower - volumetricSource  =  imbalance,
//
// and `imbalance` is the single number a conservation gate reads -- but never the only number
// reported, because a cancellation between two large terms would hide inside it.
//
// WHY THE CONVECTIVE FLUX CARRIES hTotal AND NOT cp*T
// --------------------------------------------------
// Because rho hTotal = rhoE + p is an identity of the contract (TotalEnergy.hpp), the inviscid
// flux (rho eTotal + p) U is exactly m_dot_f hTotal_f. That is the whole reason total enthalpy
// appears: the pressure work is IN the flux, not a separate term that could be forgotten.
// Transporting cp*T instead -- which the existing P15 thermal path does, and which P15's own
// Enthalpy.hpp calls wrong for varying cp -- was measured at 26-103% error in
// results/p17/total_energy/logs/01_dryrun.log section E. This path never does it.
//
// WHAT IS NOT HERE, so that nothing is over-claimed
// ------------------------------------------------
//   * There is NO implicit rhoE solve. hTotal is a nonlinear function of the unknown rhoE through
//     T, so an implicit form needs a Newton linearisation -- solver architecture that belongs with
//     the density-based solver, not with this phase. What is provided is the conservative flux
//     form plus an EXPLICIT conservative advance, which is enough to march the closed-domain,
//     uniform-heating and adiabatic-compression verifications and to measure a global balance.
//   * The viscous work term is limited by what P17.4's stress model supports. P17.4 assembles
//     mu lap(U) plus an optional -(2/3) mu grad(div U); it does NOT build the stress TENSOR, so
//     (tau . U) . n cannot be formed exactly from it. See viscousWorkLimitation() and
//     results/p17/total_energy/viscous_work.md -- the shortfall is carried forward explicitly
//     rather than papered over with tau : grad(U), which is the INTERNAL-energy form and would
//     silently change which equation is being solved.

namespace cfd::compressible {

// Every term of the global balance, reported separately. Section 20 of the authorization requires
// exactly this: "Do not hide cancellations inside one opaque scalar."
struct TotalEnergyBalance {
  Real accumulation{};       // sum over cells of (rhoE_new - rhoE_old) V / dt   [W]
  Real convectiveOutflow{};  // net boundary sum of m_dot_f hTotal_f              [W]
  Real conductiveOutflow{};  // net boundary sum of q_f . n_f A_f                 [W]
  Real viscousWorkInflow{};  // net boundary sum of (tau . U)_f . n_f A_f         [W]
  Real bodyForcePower{};     // sum over cells of rho f . U V                     [W]
  Real volumetricSource{};   // sum over cells of S_E V                           [W]
  Real imbalance{};          // accumulation + convOut + condOut - viscIn - body - source
  Real scale{};              // the largest single term, the magnitude the bound is derived from
  Real bound{};              // kAccum * eps * N * scale
  [[nodiscard]] bool withinBound() const noexcept;
};

// kAccum is EMPIRICAL and is labelled as such. Reused verbatim from P17.1/P17.3/P17.4 rather than
// re-derived, so conservation bounds stay comparable across the P17 phases.
constexpr Real kEnergyAccumulationFactor = 8.0;

// --- the individual terms, each the integrated net OUTFLOW in watts -------

// sum_f m_dot_f hTotal_f, upwinded on the mass flux. Conservative BY CONSTRUCTION: an internal
// face's contribution is computed once and added to the owner and subtracted from the neighbour,
// so internal faces cancel exactly in a global sum. `hTotalBoundary` supplies the total enthalpy
// on boundary faces where the flux enters the domain; on an outflow face the owner cell's value is
// used, which is the same upwind convention the P17.3 mass flux already applies to density.
// Throws InvalidArgumentError on size mismatches or non-finite entries.
[[nodiscard]] cfd::fields::ScalarField totalEnergyConvectiveDivergence(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::ScalarField& specificTotalEnthalpy,
    const cfd::fields::SurfaceField& hTotalBoundary);

// sum_f q_f . n_f A_f with q = -k grad T, i.e. -div(k grad T) * V.
// Reuses discretization::diffusion, which is the qualified operator whose internal faces cancel
// exactly ("face-once conservation") and whose boundary handling is P15's. No second conduction
// discretisation and no private conductivity correlation is introduced.
// Throws InvalidArgumentError if conductivity is not finite and > 0.
[[nodiscard]] cfd::fields::ScalarField totalEnergyConductiveDivergence(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature, Real conductivity,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries);

// (rhoE_new - rhoE_old) V / dt, with the two states kept SEPARATE.
// This is d(rho eTotal)/dt, NOT rho d(eTotal)/dt: for variable density the two differ, and a test
// in which BOTH rho and eTotal change is what distinguishes them.
// Throws InvalidArgumentError if dt is not finite and > 0.
[[nodiscard]] cfd::fields::ScalarField totalEnergyAccumulation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& rhoEOld,
    const cfd::fields::ScalarField& rhoENew, Real timeStep);

// rho f . U V per cell [W]. Zero whenever U = 0, exactly.
[[nodiscard]] cfd::fields::ScalarField bodyForcePowerField(const cfd::mesh::Mesh& mesh,
                                                           const cfd::fields::ScalarField& density,
                                                           const cfd::fields::VectorField& velocity,
                                                           Vector2 bodyAcceleration);

// The global balance. `viscousWork` and `source` are per-cell integrated fields [W], passed in so
// the caller owns whichever model produced them; null means the term is absent and is reported as
// exactly 0 rather than silently folded into the imbalance.
[[nodiscard]] TotalEnergyBalance evaluateTotalEnergyBalance(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& accumulation,
    const cfd::fields::ScalarField& convectiveDivergence,
    const cfd::fields::ScalarField& conductiveDivergence,
    const cfd::fields::ScalarField* viscousWork, const cfd::fields::ScalarField* bodyForcePower,
    const cfd::fields::ScalarField* volumetricSource);

// One EXPLICIT conservative step:
//   rhoE_new = rhoE_old - (dt/V) [ convOut + condOut - viscIn - bodyPower - source ]
// Conservative because every flux term it consumes is conservative: what leaves one cell through
// an internal face enters its neighbour through the same face, computed once.
// Throws InvalidArgumentError if dt is not finite and > 0, or on a size mismatch.
[[nodiscard]] cfd::fields::ScalarField advanceTotalEnergyExplicit(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& rhoEOld,
    const cfd::fields::ScalarField& convectiveDivergence,
    const cfd::fields::ScalarField& conductiveDivergence,
    const cfd::fields::ScalarField* viscousWork, const cfd::fields::ScalarField* bodyForcePower,
    const cfd::fields::ScalarField* volumetricSource, Real timeStep);

// What the viscous-work term can and cannot be, in one machine-readable place so a caller cannot
// believe more than P17.4 delivered.
struct ViscousWorkLimitation {
  bool stressTensorAvailable{false};  // P17.4 builds mu lap(U), not tau_ij
  const char* description{};
};
[[nodiscard]] ViscousWorkLimitation viscousWorkLimitation() noexcept;

}  // namespace cfd::compressible
