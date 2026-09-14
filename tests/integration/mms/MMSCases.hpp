#pragma once

// P12-NUM-006 -- system-level method-of-manufactured-solutions cases.
// Each run*Level function performs one PRODUCTION solve (ThermalSolver,
// the relaxed momentum assembly + linear solver, or SIMPLE) on one grid
// with the analytical forcing of tests/unit/discretization/
// ManufacturedFields.hpp (namespace mms), applies the solve-validity gate
// (status Converged, finite fields, mass balance) and measures the error
// against the manufactured fields with the one norm implementation
// (cfd/validation/ErrorNorms.hpp). Nothing here builds a forcing term from
// a discrete operator: the forcing comes from the analytical expressions,
// sampled at cell centroids.

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/validation/GridConvergence.hpp"
#include "cfd/validation/ManufacturedSolutionStudy.hpp"

namespace cfd::test::mmscase {

// --- Physical coefficients (fixed with the manufactured solution) -----------
// Scalar: rho cp div(U phi) - k lap(phi) = Q.
constexpr Real kScalarDensity = 1.2;
constexpr Real kScalarSpecificHeat = 1.5;
constexpr Real kScalarConductivity = 0.2;  // Pe = rho cp |U| L / k ~ 13
// Diffusion-dominated variant for the distorted-mesh study (Pe ~ 0.5):
// there the NON-ORTHOGONAL DIFFUSION error is visible, not swamped by the
// first-order upwind convection error.
constexpr Real kScalarDiffusiveConductivity = 5.0;
// Flow: rho (U.grad)U - mu lap(U) = -grad p + f.
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;  // Re = rho |U| L / mu ~ 16
// Distortion amplitude (fraction of h) of DistortedMesh.hpp's meshes.
constexpr Real kDistortion = 0.25;

std::string schemeName(cfd::discretization::ConvectionScheme scheme);

// --- Scalar advection-diffusion (ThermalSolver, per-cell source) -------------
struct ScalarOptions {
  Real conductivity{kScalarConductivity};
  bool distorted{false};
  bool nonOrthogonalCorrection{false};  // least-squares gradient
};
// Errors: "phi", "phi_interior", "phi_boundary_ring".
cfd::validation::MMSLevel runScalarLevel(Index n, const ScalarOptions& options);

// --- Momentum only (exact pressure, exact face flux, analytical forcing) -----
// The relaxed momentum assembly SIMPLE uses (alpha = 1: no relaxation),
// solved by Picard iteration on the deferred convection correction with
// the linear-solver fallback policy. The face mass flux is the EXACT
// integral of rho U.n (divergence-free per cell), frozen: this isolates the
// momentum discretization from face-flux interpolation and from the
// pressure-velocity coupling.
struct MomentumOptions {
  cfd::discretization::ConvectionScheme scheme{cfd::discretization::ConvectionScheme::Upwind};
  bool distorted{false};
  bool nonOrthogonalCorrection{false};  // + least-squares gradient
};
// Errors: "u", "v", "velocity", "u_interior", "v_interior",
// "u_boundary_ring", "v_boundary_ring".
cfd::validation::MMSLevel runMomentumLevel(Index n, const MomentumOptions& options);

// --- Full SIMPLE ---------------------------------------------------------------
struct SimpleOptions {
  cfd::discretization::ConvectionScheme scheme{cfd::discretization::ConvectionScheme::Upwind};
  bool distorted{false};  // + nonOrthogonalCorrections 2, least-squares gradient
  Real tolerance{1e-8};   // outer velocity/pressure/continuity tolerance
};
// Starts from u = 0, p = 0 (not the exact solution). Errors: "u", "v",
// "velocity", "p" (gauge-invariant: zero volume-weighted mean), the
// "_interior" / "_boundary_ring" splits of u, v and p, "continuity"
// (cell mass imbalance of the converged face flux per unit volume) and
// "face_flux" (error of the converged face flux against the exact face
// flux, per unit face area). Diagnostics: "global_mass_imbalance",
// "final_u_residual", "final_continuity_residual".
cfd::validation::MMSLevel runSimpleLevel(Index n, const SimpleOptions& options);

// The SIMPLE result fields of one run, for determinism / initial-condition
// checks.
struct SimpleFields {
  std::vector<Real> u;
  std::vector<Real> v;
  std::vector<Real> p;
  std::string status;
  Index iterations{0};
};
SimpleFields runSimpleFields(Index n, const SimpleOptions& options, Real initialVelocityScale,
                             Real initialPressure);

// --- CompressibleSIMPLE (isothermal ideal gas) -----------------------------------
// rho = (P_ref + p)/(R T0) with P_ref = R T0 = 20 (density 0.95-1.08, div U
// != 0). Measured: the stronger coupling P_ref = R T0 = 5 (density
// 0.8-1.3) diverges on 32x32 with the standard settings (pseudo time step
// 1) and needs a pseudo time step of 0.1 and 24000 outer iterations there
// -- a stability/cost limit of the pseudo-transient formulation, recorded
// in results/p12-num-006/summary.md, not hidden. Mass
// flux m = rho U = the vortex field (div(rho U) = 0 identically), forcing
// mms::compressibleMomentumForcing through CompressibleSIMPLE::
// setMomentumSource. The closed box has Neumann pressure data, and
// CompressibleSIMPLE pins p' = 0 at its reference cell, so the reference
// cell keeps its initial pressure: in a closed compressible domain the
// absolute pressure level is a physical datum (the total mass), supplied
// as ONE scalar -- the initial pressure is uniform, equal to the exact
// gauge pressure at the reference cell's centroid (not the solution
// field); velocity starts at rest. Errors: "u", "v", "velocity", "p"
// (absolute gauge pressure -- the level is pinned, not free), "p_gauge"
// (zero-mean), "density", "continuity", "mass_flux" (vs the exact face
// mass flux, per unit area). Diagnostics: "eos_max_relative_deviation"
// (max |rho - EOS(P_ref + p, T0)| / rho of the converged state).
constexpr Real kCompressibleReferencePressure = 20.0;
constexpr Real kGasConstant = 1.0;
constexpr Real kIsothermalTemperature = 20.0;
cfd::validation::MMSLevel runCompressibleLevel(Index n);

// --- Study assembly / gates ------------------------------------------------------
// Observed order of `quantity`/`norm` (NUM-005 analysis, formal order
// `formalOrder`) appended to the study and returned.
const cfd::validation::MMSOrder& addOrder(cfd::validation::MMSStudy& study,
                                          const std::string& quantity,
                                          cfd::validation::NormKind norm, Real formalOrder);
// Records a gate and returns its verdict (the test also asserts it).
bool addGate(cfd::validation::MMSStudy& study, const std::string& name, bool passed,
             const std::string& detail);
// Every level accepted (Converged, finite, mass-balanced).
bool allLevelsAccepted(const cfd::validation::MMSStudy& study);
// The quantity's norm decreases strictly from each level to the next.
bool monotonicallyDecreasing(const cfd::validation::MMSStudy& study, const std::string& quantity,
                             cfd::validation::NormKind norm);

// Adds the order of quantity/norm and a gate "<category>: <quantity>
// <norm> observed order in [lo, hi]" on the FINEST triplet's observed
// order (NUM-005 analysis). The band is the scheme's expected order class
// (1 for upwind convection, 2 for central/linear-upwind with second-order
// diffusion/gradients), with the measured pre-asymptotic spread -- see
// each call site for the justification.
bool addOrderGate(cfd::validation::MMSStudy& study, const std::string& category,
                  const std::string& quantity, cfd::validation::NormKind norm, Real formalOrder,
                  Real lo, Real hi);
// Gate "<category>: <quantity> L1/L2/Linf decrease on every refinement".
bool addDecreaseGate(cfd::validation::MMSStudy& study, const std::string& category,
                     const std::string& quantity);
// Every gate whose name starts with "<category>:" passed (and >= 1 exists);
// failing gates are listed in `failures`.
bool categoryPassed(const cfd::validation::MMSStudy& study, const std::string& category,
                    std::string* failures);

// Writes results/validation/mms/<file>.json and .md and prints the
// Markdown to stdout (test log evidence).
void writeStudy(const cfd::validation::MMSStudy& study, const std::string& file);

std::string format(const char* fmt, Real value);

}  // namespace cfd::test::mmscase
