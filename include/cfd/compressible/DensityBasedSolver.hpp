#pragma once

#include <map>
#include <string>
#include <vector>

#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

// P17.8 -- the density-based compressible solver.
//
// It evolves the CONSERVED state Q = [rho, rho U, rhoE] directly, using P17.7's HLLC flux for the
// inviscid face flux, an explicit single-stage update, and an explicit CFL-controlled time step.
//
// WHAT THIS SOLVER IS, AND IS NOT
// ------------------------------
//   * EULER ONLY. The face flux is HLLC and nothing else: no viscous stress, no heat conduction.
//     Compressible Navier-Stokes is NOT claimed. The authorization's section 8 permits this
//     provided it is documented, and the seam for adding those terms is described in
//     results/p17/density_based_solver/architecture.md section 5 -- deliberately with NO zero stub,
//     because a zero stub is indistinguishable from a wired-up term that happens to vanish.
//   * LIBRARY LEVEL. It is not reachable from a case file, and no dispatch or case-format key is
//     added. That is why CompressibleSIMPLE and both SIMPLE paths are provably unaffected: no line
//     that selects a solver is touched. Stated plainly rather than implied.
//   * HOST ONLY. There is no GPU density-based solver in P17.8; the frozen gate matrix does not ask
//     for one. P17.7's HLLC flux is already bitwise identical on CPU and GPU, so the flux half of a
//     future GPU solver is qualified; the residual assembly, the CFL reduction and the update are
//     not.
//   * RESTART UNSUPPORTED, and it refuses -- see restartSupported() / throwIfRestartRequested().
//
// NOTHING HERE DUPLICATES UPSTREAM PHYSICS
// ----------------------------------------
// There is no equation of state, no sound-speed formula, no energy relation and no Riemann solver
// in this file or its .cpp. Every one of those comes from P17.2
// (ThermodynamicProperties/IdealGasEOS), P17.5 (TotalEnergyState) or P17.7 (HllcFlux.hpp), and the
// solver only composes them.
//
// WHY NOT solver::TransientSolver
// -------------------------------
// Its shape is right -- an injected step solver, a history, a pre-step CFL guard, rejection, and
// "the final state is the last ACCEPTED state". But its state type is
//     struct TransientState { VectorField velocity; ScalarField pressure; };
// which cannot hold [rho, rhoU, rhoE]. It is a P2 class used by PISO, so widening it would change a
// shared type for every existing caller. This solver therefore owns its own loop and MIRRORS those
// conventions without modifying it. The similarity is deliberate; the duplication is forced.

namespace cfd::compressible {

// ---------------------------------------------------------------------------------------------
// The conserved state: ONE owner (authorization section 4)
// ---------------------------------------------------------------------------------------------
//
// Primitives are deliberately NOT stored alongside Q. They are recomputed from Q on demand by
// toPrimitive(), which routes through P17.5's recover(), so they cannot drift out of step with the
// conserved state -- which is the property section 4's "avoid multiple unsynchronized copies"
// actually protects.
struct ConservedField {
  cfd::fields::ScalarField density;             // rho   [kg/m^3]
  cfd::fields::VectorField momentumDensity;     // rho U [kg/(m^2 s)]
  cfd::fields::ScalarField totalEnergyDensity;  // rhoE  [J/m^3]

  [[nodiscard]] Index size() const noexcept { return static_cast<Index>(density.size()); }
};

// The finite-volume residual, R_P = sum_f (+/-) F_f A_f. Same shape as ConservedField but different
// units, so it is a distinct type: passing one where the other is expected is a compile error.
struct ResidualField {
  cfd::fields::ScalarField density;      // [kg/s]
  cfd::fields::VectorField momentum;     // [N]
  cfd::fields::ScalarField totalEnergy;  // [W]
};

// Volume-weighted L2 norms, one per conserved equation. The norm is of R_P/V_P -- a per-unit-volume
// RATE -- so it does not scale with mesh size, which section 16 explicitly requires.
struct ResidualNorms {
  Real density{};
  Real momentumX{};
  Real momentumY{};
  Real momentumZ{};
  Real totalEnergy{};

  [[nodiscard]] Real maximum() const noexcept;
};

enum class TimeSteppingMode {
  // The SAME dt for every cell, equal to min_P dt_P. The only physically meaningful transient mode.
  GlobalPhysical,
  // A per-cell dt, for steady-state convergence acceleration. NOT a transient integrator: cells
  // advance different amounts of physical time, so the intermediate states are not a time history
  // of
  // anything. DensityBasedResult refuses to report a physical time in this mode -- see
  // hasPhysicalTime -- which is how section 12's warning is enforced rather than merely written
  // down.
  LocalPseudoTime
};

// The three boundary treatments P17.8 implements. Each supplies an EXTERIOR state to P17.7's
// boundary interface; no boundary logic lives inside HLLC (section 15).
//
// NOT IMPLEMENTED and NOT claimed: subsonic inlet/outlet with characteristic (Riemann-invariant)
// treatment, and far field. A subsonic open boundary needs incoming-characteristic reconstruction
// to be well posed, which is its own verification surface. The canonical cases need only these
// three.
enum class BoundaryKind {
  // U_ext = U_int - 2 (U_int . n) n, with rho, p and T unchanged. The mirrored normal velocity
  // makes
  // the contact speed vanish, so the mass and energy fluxes vanish and only the pressure force
  // remains -- section 15's requirement, achieved by the state rather than by a special case.
  InviscidWall,
  // Every characteristic enters: the exterior state is the prescribed one, used as given.
  SupersonicInflow,
  // Every characteristic leaves: the exterior state is the interior state (zero-gradient).
  SupersonicOutflow
};

struct DensityBasedBoundary {
  BoundaryKind kind{BoundaryKind::InviscidWall};
  // Used only by SupersonicInflow. Built through makePrimitiveState(), so it carries p, T, a and
  // rhoE from the authoritative models like any other state.
  PrimitiveState prescribed{};
};

using DensityBasedBoundaries = std::map<std::string, DensityBasedBoundary>;

struct DensityBasedSettings {
  Real cfl{0.4};
  TimeSteppingMode mode{TimeSteppingMode::GlobalPhysical};

  // GlobalPhysical stops at endTime (status TimeComplete) or maxIterations, whichever first.
  // LocalPseudoTime stops on the steady criteria below, or maxIterations.
  Real endTime{0.0};
  Index maxIterations{1000};

  // Steady convergence, evaluated ONLY in LocalPseudoTime mode (section 17: steady stopping
  // criteria must not be applied to a transient run). ALL THREE groups must hold -- no single
  // signal suffices.
  Real residualTolerance{1e-8};        // on every one of the five RELATIVE norms
  Real massImbalanceTolerance{1e-10};  // on the global mass imbalance, relative to the total mass
  Real stateChangeTolerance{1e-10};    // on max |Q^{n+1} - Q^n| / |Q^n|

  // Deterministic retry on an inadmissible candidate update (section 19). Nothing is ever clamped.
  Index maxRetries{5};
  Real retryReductionFactor{0.5};
};

enum class DensityBasedStatus {
  // LocalPseudoTime only: all three steady criteria met.
  Converged,
  // GlobalPhysical only: endTime reached. DELIBERATELY DISTINCT from Converged -- reaching the end
  // of
  // a transient run is not convergence, and section 17 warns against conflating them.
  TimeComplete,
  MaxIterations,
  Stagnated,
  // A candidate update was inadmissible and could not be rescued within maxRetries.
  InvalidState,
  NonFiniteState,
  InvalidConfiguration
};

struct DensityBasedStepRecord {
  Index iteration{};
  Real time{};  // meaningful only in GlobalPhysical mode
  Real dt{};    // the GLOBAL dt, or the minimum local dt in LocalPseudoTime
  ResidualNorms absolute{};
  ResidualNorms relative{};
  Real massImbalance{};
  Real minDensity{};
  Real minPressure{};
  Real minTemperature{};
  Real maxMach{};
  Index retries{};  // retries spent ON THIS STEP
};

struct DensityBasedResult {
  DensityBasedStatus status{DensityBasedStatus::InvalidConfiguration};
  // The last ACCEPTED state. For any non-Converged/TimeComplete status this is the state the
  // failing step was computed FROM, never that step's own discarded output -- the same convention
  // solver::TransientSolver documents.
  ConservedField state;
  std::vector<DensityBasedStepRecord> history;

  Index iterations{};
  Index totalRetries{};
  Index rejectedSteps{};
  Index nonFiniteDetections{};

  // false in LocalPseudoTime mode. physicalTime() throws when this is false, so a number that means
  // nothing can never be read out of a pseudo-time run.
  //
  // The time itself is PRIVATE and reachable only through that accessor -- if it were a public
  // member, section 12's guard would be advice rather than enforcement, and a caller could read a
  // pseudo-time "time" that is not any cell's physical time.
  bool hasPhysicalTime{false};
  [[nodiscard]] Real physicalTime() const;

  // Set when status is InvalidState or NonFiniteState: the FIRST offending cell in deterministic id
  // order, and a message naming the quantity and its value.
  Index offendingCell{};
  std::string diagnostic;

  [[nodiscard]] const ResidualNorms& finalAbsoluteResiduals() const;
  [[nodiscard]] const ResidualNorms& finalRelativeResiduals() const;

 private:
  friend class DensityBasedSolver;
  Real time_{};
};

// ---------------------------------------------------------------------------------------------
// The solver
// ---------------------------------------------------------------------------------------------

class DensityBasedSolver {
 public:
  // Throws InvalidArgumentError if cfl is not finite or <= 0, if maxIterations is 0, if
  // retryReductionFactor is not in (0,1), if a tolerance is not finite and > 0, or if
  // GlobalPhysical mode is selected with endTime <= 0.
  DensityBasedSolver(DensityBasedSettings settings, TotalEnergyState thermodynamics);

  // --- state construction and conversion (section 5) -------------------------------------------
  //
  // Both directions go through P17.5. There is no conversion arithmetic in this class.

  // Throws through P17.2/P17.5 for any inadmissible density, temperature or velocity.
  [[nodiscard]] ConservedField fromPrimitive(const cfd::mesh::Mesh& mesh,
                                             const cfd::fields::ScalarField& density,
                                             const cfd::fields::VectorField& velocity,
                                             const cfd::fields::ScalarField& temperature) const;

  // Q -> primitives, per cell, via P17.5's recover(). Throws NumericalError naming the cell if any
  // cell's conserved state is not a valid thermodynamic state. NOTHING is clamped.
  [[nodiscard]] std::vector<PrimitiveState> toPrimitive(const ConservedField& q) const;

  // --- the pieces, exposed so they can be tested in isolation (sections 34, 35) -----------------

  // R_P = sum_f (+F_f A_f for the owner, -F_f A_f for the neighbour), the repository's convention
  // (src/compressible/CompressibleContinuity.cpp). ONE hllcFlux evaluation per face produces all
  // five components; the same value is added to the owner and subtracted from the neighbour, so the
  // cancellation is exact by construction rather than to round-off.
  [[nodiscard]] ResidualField assembleResidual(const cfd::mesh::Mesh& mesh,
                                               const std::vector<PrimitiveState>& primitives,
                                               const DensityBasedBoundaries& boundaries) const;

  // dt_P = CFL * V_P / sum_f (|U_f.n_f| + a_f) A_f, with the face state the arithmetic mean of the
  // two cell states (the interior state alone at a boundary face).
  //
  // THIS IS NOT solver::calculateCFL AND DOES NOT REPLACE IT. That function is diagnostic only,
  // advective only, constant-density, and maps dt -> Co; this one maps Co -> dt and carries the
  // acoustic term an explicit compressible step needs. The two are tied together by a verified
  // relation: with a = 0 and a uniform field, feeding this dt back through calculateCFL returns
  // CFL/2, because that function carries a documented 1/2 factor. See
  // results/p17/density_based_solver/cfl.md.
  [[nodiscard]] std::vector<Real> localTimeSteps(
      const cfd::mesh::Mesh& mesh, const std::vector<PrimitiveState>& primitives) const;

  // min over cells of localTimeSteps(). Throws NumericalError if no cell yields a finite positive
  // dt.
  [[nodiscard]] Real globalTimeStep(const cfd::mesh::Mesh& mesh,
                                    const std::vector<PrimitiveState>& primitives) const;

  // Q_new = Q_old - (dt_P / V_P) R_P, component by component. `dt` must have one entry per cell
  // (GlobalPhysical passes the same value in every entry, which U2 asserts).
  [[nodiscard]] static ConservedField applyUpdate(const cfd::mesh::Mesh& mesh,
                                                  const ConservedField& q, const ResidualField& r,
                                                  const std::vector<Real>& dt);

  // Volume-weighted L2 of R_P/V_P, per equation.
  [[nodiscard]] static ResidualNorms residualNorms(const cfd::mesh::Mesh& mesh,
                                                   const ResidualField& r);

  // --- the solve -------------------------------------------------------------------------------

  // Takes Q by value and returns the final state, mirroring SIMPLE::solve() and
  // TransientSolver::solve(): the caller's field is never mutated.
  [[nodiscard]] DensityBasedResult solve(const cfd::mesh::Mesh& mesh, ConservedField q,
                                         const DensityBasedBoundaries& boundaries) const;

  // --- scope boundaries that REFUSE rather than mislead (section 29) ----------------------------

  // false. The existing restart format (io/RestartWriter.hpp) serialises a fixed payload --
  // pressure, velocity_x, velocity_y, mass_flux -- with no rho, rhoU or rhoE, and widening it is an
  // IO-format change affecting every existing caller. Section 29 permits UNSUPPORTED provided the
  // solver refuses explicitly instead of silently producing wrong state.
  [[nodiscard]] static bool restartSupported() noexcept { return false; }

  // Always throws, naming the conserved fields the format cannot carry.
  static void throwIfRestartRequested();

  [[nodiscard]] const DensityBasedSettings& settings() const noexcept { return settings_; }
  [[nodiscard]] const TotalEnergyState& thermodynamics() const noexcept { return thermodynamics_; }

 private:
  DensityBasedSettings settings_;
  TotalEnergyState thermodynamics_;
};

// The exterior state for a boundary face, given the interior state and the OUTWARD unit normal.
// Exposed for testing: the wall's mirrored normal velocity is the property section 15 turns on.
[[nodiscard]] PrimitiveState exteriorState(const DensityBasedBoundary& boundary,
                                           const PrimitiveState& interior,
                                           const Vector3& outwardUnitNormal);

}  // namespace cfd::compressible
