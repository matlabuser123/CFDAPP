#pragma once

// P12-NUM-007 -- the lid-driven cavity as a production validation case:
// one function that solves a (Reynolds number, grid, convection scheme)
// combination with the documented per-grid SIMPLE settings of
// test_cavity_ghia.cpp (plus the P12-NUM-004 linear-solver fallback) and
// records it as a cfd::validation::ValidationRun -- Ghia et al. (1982)
// centerline errors (L1/L2/Linf over the tabulated stations), solver
// status, iterations, linear iterations, mass imbalance, runtime and the
// physical checks. Shared by test_cavity_production_validation.cpp and
// test_scheme_validation.cpp; validation-only, never part of cfdcore.

#include <array>
#include <string>
#include <vector>

#include "CavityValidationUtils.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"
#include "cfd/validation/ProductionValidation.hpp"

namespace cfd::validation::cavity {

inline constexpr Real kLidVelocity = 1.0;
inline constexpr Real kDensity = 1.0;
// Outer convergence / mass gates: the same 1e-6 the P0 cavity tests use.
inline constexpr Real kOuterTolerance = 1e-6;
inline constexpr Real kMassImbalanceTolerance = 1e-6;

struct CavityRunSpec {
  Real reynolds{100.0};  // 100 or 1000 (the Ghia tables embedded here)
  Index n{20};           // n x n cells on the unit square
  discretization::ConvectionScheme scheme{discretization::ConvectionScheme::Upwind};
};

// Per-grid SIMPLE settings: relaxation 0.7/0.3, outer tolerances 1e-6,
// momentum BiCGSTAB 1e-10/1e-8, pressure BiCGSTAB with the grid's
// documented inner tolerance (test_cavity_ghia.cpp header: 20 -> 1e-10/
// 1e-8, 40 -> 1e-8/1e-6, >= 80 -> 1e-7/1e-5), NUM-004 fallback enabled.
// Identical for every Reynolds number and convection scheme on a grid.
[[nodiscard]] pressure_velocity::SIMPLESettings cavitySettings(const CavityRunSpec& spec);

// "cavity_re100", "cavity_re1000".
[[nodiscard]] std::string caseName(Real reynolds);

// The Ghia stations used as grid-convergence quantities for this Reynolds
// number (names are keys of the run's diagnostics) and their Ghia values.
struct Station {
  std::string name;
  std::string description;
  Real ghia;
};
[[nodiscard]] std::vector<Station> convergenceStations(Real reynolds);

// Solves spec and records it. Errors: "u_centerline" (u(0.5, y) at Ghia's
// 17 y-stations) and "v_centerline" (v(x, 0.5) at the 17 x-stations),
// unweighted over the stations. Diagnostics: the SIMPLE cost/residual set,
// "max_wall_normal_flux", "max_velocity_magnitude", and the
// convergenceStations values. Checks: "solve_accepted",
// "wall_normal_flux" (<= 1e-6). If csvDirectory is non-empty the two
// centerline comparisons are written there as CSV.
[[nodiscard]] ValidationRun runCavity(const CavityRunSpec& spec,
                                      const std::string& csvDirectory = {});

// Adds the regression check "ghia_error": u and v centerline L2 errors at
// or below the given bounds.
void addGhiaErrorCheck(ValidationRun& run, Real maxUL2, Real maxVL2);

// Adds "bounded": max |U| over the cells <= the lid speed (to 1e-9) -- the
// cavity velocity never exceeds the lid's; an overshoot marks a scheme's
// loss of boundedness.
void addBoundednessCheck(ValidationRun& run);

// P12-NUM-005 three-grid study (coarse, medium, fine) of the convergence
// stations, analysed from the runs' own solutions (no re-solve); the Ghia
// values are attached as "benchmark" references (reported, never used
// for the order).
[[nodiscard]] GridConvergenceStudy cavityGridConvergence(
    const std::string& name, const std::string& description,
    const std::array<const ValidationRun*, 3>& coarseToFine, Real reynolds, Real formalOrder);

}  // namespace cfd::validation::cavity
