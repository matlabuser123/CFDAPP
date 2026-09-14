#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cfd/core/Types.hpp"

// P12-NUM-005 -- three-grid convergence analysis: observed order, Richardson
// extrapolation and the Grid Convergence Index, for ONE scalar quantity of
// interest. The only implementation of these formulas in the codebase:
// validation studies call analyzeGridConvergence(), never re-derive it.
//
// Method: Celik, Ghia, Roache, Freitas, Coleman & Raad, "Procedure for
// Estimation and Reporting of Uncertainty Due to Discretization in CFD
// Applications", J. Fluids Eng. 130 (2008) 078001 (the ASME JFE standard
// procedure), which builds on Roache, "Verification and Validation in
// Computational Science and Engineering" (1998).
//
// INDEXING CONVENTION (used everywhere, reports included):
//   1 = FINE, 2 = MEDIUM, 3 = COARSE;   h1 < h2 < h3
//   r21 = h2 / h1 > 1,   r32 = h3 / h2 > 1
//   epsilon21 = phi2 - phi1,   epsilon32 = phi3 - phi2
//
// UNITS: every relative quantity (approximate/extrapolated relative errors,
// GCI) is a FRACTION (0.01 = 1 %), never a percentage. Absolute
// uncertainties are in the quantity's own units.
namespace cfd::validation {

// Representative (characteristic) grid size h = (measure / cells)^(1/dim):
// the average cell size of a mesh of `cells` cells covering a domain of
// area (dim = 2) or volume (dim = 3) `measure` (Celik et al. eq. 1). For a
// uniform nx x ny Cartesian mesh on Lx x Ly this is sqrt(dx * dy) -- equal
// to 1/nx only for a square domain with nx = ny, which is why it is not
// assumed. Throws InvalidArgumentError if measure or cells is not positive
// and finite, or dimension is not 1, 2 or 3.
[[nodiscard]] Real representativeGridSize(Real measure, Index cells, int dimension = 2);

struct GridLevel {
  Real h{};      // representative grid size (any consistent length unit)
  Real value{};  // the quantity of interest on this grid
};

// How the three values behave as the grid is refined.
enum class ConvergenceClass {
  Monotonic,               // epsilon21, epsilon32 same sign, observed order > 0
  Oscillatory,             // epsilon21, epsilon32 opposite signs
  Divergent,               // differences do not shrink (apparent order <= 0)
  InsufficientSeparation,  // a difference is within the noise level, or order unbounded
  Invalid,                 // inputs rejected (non-finite, bad grid sizes/ordering)
};

enum class GridConvergenceStatus {
  // Monotonic, order found, and the asymptotic-range check (against the
  // formal order) passed.
  Asymptotic,
  // Monotonic, order found, asymptotic-range check failed.
  MonotonicNotAsymptotic,
  // Monotonic, order found, no formal order supplied so the asymptotic
  // range cannot be assessed from three grids (see asymptoticRatio).
  MonotonicAsymptoticRangeUnknown,
  Oscillatory,
  Divergent,
  InsufficientSeparation,
  Invalid,
};

[[nodiscard]] std::string_view convergenceClassName(ConvergenceClass value) noexcept;
[[nodiscard]] std::string_view gridConvergenceStatusName(GridConvergenceStatus value) noexcept;

struct GridConvergenceOptions {
  // Fs. 1.25 is the value Roache and Celik et al. recommend for a
  // three-grid study with an OBSERVED order (3.0 is for two grids with an
  // assumed order).
  Real safetyFactor{1.25};
  // The formal (theoretical) order of the discretization for this quantity.
  // Needed for the asymptotic-range check -- see asymptoticRatio.
  std::optional<Real> formalOrder;
  // Asymptotic band: |asymptoticRatio - 1| <= asymptoticTolerance.
  Real asymptoticTolerance{0.1};
  // A difference |epsilon| <= max(absoluteNoise, relativeNoise * max|phi|)
  // is treated as zero (below the iterative/round-off noise of the values).
  // Studies should set absoluteNoise to their iterative-convergence noise.
  Real absoluteNoise{0.0};
  Real relativeNoise{1e-12};
  // Refinement ratios below this are rejected as "nearly identical grids"
  // (Invalid); Celik et al. recommend r > 1.3 -- ratios in
  // [minimumRefinementRatio, 1.3) are accepted with a warning.
  Real minimumRefinementRatio{1.1};
  // Observed orders above this are not trusted (a near-zero epsilon21
  // relative to epsilon32 -- coincidental cancellation or super-convergence).
  Real maximumOrder{20.0};
  // Grid-independence criterion: GCI21 (fraction) <= this, plus status
  // Asymptotic. Unset -> gridIndependent is false ("no threshold configured")
  // -- there is no universally valid engineering threshold, so the study
  // chooses and justifies one.
  std::optional<Real> gridIndependenceThreshold;
};

struct GridConvergenceResult {
  GridConvergenceStatus status{GridConvergenceStatus::Invalid};
  ConvergenceClass convergence{ConvergenceClass::Invalid};
  std::string diagnostic;             // always non-empty: why this status
  std::vector<std::string> warnings;  // non-fatal notes (e.g. r < 1.3)

  Real r21{};
  Real r32{};
  Real epsilon21{};
  Real epsilon32{};
  // R = epsilon21 / epsilon32 (Celik et al.): 0 < R < 1 monotonic, R < 0
  // oscillatory. Unset when epsilon32 is within noise.
  std::optional<Real> convergenceRatio;

  // Observed order p: the p > 0 solving
  //   epsilon32 / epsilon21 = r21^p (r32^p - 1) / (r21^p - 1)
  // (exact for phi(h) = phi_exact + C h^p; reduces to
  // p = ln(epsilon32/epsilon21) / ln r for r21 = r32 = r). Solved by
  // bisection -- the left side is strictly increasing in p -- so unequal
  // ratios are handled exactly, not approximated. Only set for monotonic
  // sequences.
  std::optional<Real> observedOrder;
  Index orderIterations{0};

  // Richardson extrapolation, fine pair and medium pair:
  //   phi_ext21 = phi1 - epsilon21 / (r21^p - 1)
  //   phi_ext32 = phi2 - epsilon32 / (r32^p - 1)
  std::optional<Real> extrapolated21;
  std::optional<Real> extrapolated32;

  // Relative errors (fractions); unset when the denominator is within noise
  // (a quantity whose converged value is ~0 has no meaningful relative
  // error -- use the absolute uncertainties then).
  //   ea21 = |epsilon21 / phi1|,  ea32 = |epsilon32 / phi2|
  //   eext21 = |(phi_ext21 - phi1) / phi_ext21|,
  //   eext32 = |(phi_ext32 - phi2) / phi_ext32|
  std::optional<Real> approximateRelativeError21;
  std::optional<Real> approximateRelativeError32;
  std::optional<Real> extrapolatedRelativeError21;
  std::optional<Real> extrapolatedRelativeError32;

  // Absolute numerical uncertainties U = Fs |epsilon| / (r^p - 1), and the
  // Grid Convergence Index GCI = U / |phi| (fraction): GCI21 on phi1 (the
  // fine-grid uncertainty), GCI32 on phi2.
  std::optional<Real> uncertainty21;
  std::optional<Real> uncertainty32;
  std::optional<Real> gci21;
  std::optional<Real> gci32;

  // Asymptotic-range indicator (Roache): U32 / (r21^pf U21), with both
  // uncertainties computed with the FORMAL order pf. Equal to 1 when the
  // three values follow phi_exact + C h^pf exactly; > 1 when the observed
  // order exceeds pf, < 1 when it falls short. It must use the formal order:
  // with the observed order it is identically 1 for ANY monotonic triple
  // (p is fitted to exactly these three values), i.e. uninformative.
  // Uses absolute uncertainties so the ideal value is exactly 1; the
  // relative-GCI form GCI32 / (r21^pf GCI21) differs by |phi1 / phi2|.
  std::optional<Real> asymptoticRatio;
  std::optional<Real> orderRatio;  // observed / formal order

  // Oscillatory sequences: uncertainty 0.5 (max phi - min phi) (Stern et al.,
  // J. Fluids Eng. 123 (2001) 793), absolute and relative to |phi1|.
  std::optional<Real> oscillationUncertainty;
  std::optional<Real> relativeOscillationUncertainty;

  bool gridIndependent{false};
  std::string gridIndependenceReason;

  [[nodiscard]] bool orderValid() const noexcept { return observedOrder.has_value(); }
};

// The three-grid analysis. Never throws for bad data: invalid input gives
// status Invalid with the reason in `diagnostic`, and no quantity that is
// set is ever NaN or infinite.
[[nodiscard]] GridConvergenceResult analyzeGridConvergence(
    const GridLevel& fine, const GridLevel& medium, const GridLevel& coarse,
    const GridConvergenceOptions& options = {});

// The observed-order equation on its own (exposed for verification):
// the p in (0, maxOrder] with ln(r21^p (r32^p - 1)/(r21^p - 1)) = ln(ratio),
// ratio = epsilon32 / epsilon21 > 0. Unset if no such p exists (ratio at or
// below its p -> 0 limit ln r32 / ln r21: the differences do not shrink) or
// the root exceeds maxOrder.
struct ObservedOrderSolution {
  std::optional<Real> order;
  bool exceedsMaximum{false};
  Index iterations{0};
};
[[nodiscard]] ObservedOrderSolution solveObservedOrder(Real r21, Real r32, Real ratio,
                                                       Real maxOrder);

}  // namespace cfd::validation
