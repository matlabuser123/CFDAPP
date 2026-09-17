#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSolverFallback.hpp"
#include "cfd/core/Types.hpp"

// P12-NUM-004 -- outer-iteration robustness infrastructure, shared by
// cfd::pressure_velocity::SIMPLE and cfd::compressible::CompressibleSIMPLE
// (one implementation; neither solver carries its own copy of any of this):
//   - ResidualTracker: one residual history (latest / reference /
//     normalized / best / fixed-window statistics), O(1) memory per quantity;
//   - AdaptiveRelaxationController: the one under-relaxation controller;
//   - OuterIterationMonitor: the per-outer-iteration convergence decision
//     plus stagnation/divergence detection and the relaxation update.
// Every feature defaults to OFF and the default convergence criterion is the
// pre-P12-NUM-004 absolute one, evaluated with the identical comparisons --
// so a default-constructed SolverRobustnessSettings reproduces the previous
// solvers bit for bit (see results/p12-num-004/summary.md).
namespace cfd::solver {

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// Absolute (default): converged iff every residual <= its absolute tolerance
// (the pre-P12-NUM-004 rule). Normalized: momentum/pressure residuals are
// judged relative to their normalization reference (ResidualTracker), while
// continuity and global mass imbalance KEEP their absolute gates -- see
// OuterIterationMonitor.
enum class ConvergenceCriterion {
  Absolute,
  Normalized,
};

[[nodiscard]] std::string_view convergenceCriterionName(ConvergenceCriterion criterion) noexcept;

struct ResidualNormalizationSettings {
  // The reference of each residual is its largest finite value over the
  // first `referenceIterations` outer iterations (the "maximum of the first
  // five iterations" scaling convention of commercial codes, e.g. ANSYS
  // Fluent's scaled residuals). EMPIRICAL default 5: late enough that an
  // equation whose first residual is exactly 0 (e.g. v of a cavity started
  // from rest) gets a meaningful reference once the flow develops.
  Index referenceIterations{5};
  // Normalized tolerances (used only with ConvergenceCriterion::Normalized):
  // residual / reference <= tolerance. Must lie in (0, 1).
  Real velocityTolerance{1e-4};
  Real pressureTolerance{1e-4};

  bool operator==(const ResidualNormalizationSettings&) const = default;
};

struct StagnationDetectionSettings {
  bool enabled{false};
  // Rolling window W (outer iterations), 2 <= W <= kMaxDetectionWindow.
  Index window{50};
  // Minimum relative improvement of the best convergence distance over the
  // window, in (0, 1). See OuterIterationMonitor for the exact metric.
  Real minRelativeImprovement{0.01};
  // No detection before this outer iteration (startup transients).
  Index startIteration{100};

  bool operator==(const StagnationDetectionSettings&) const = default;
};

struct DivergenceDetectionSettings {
  bool enabled{false};
  // Consecutive outer iterations a growth condition must hold, 2 <= W <=
  // kMaxDetectionWindow (a single spike can never trigger).
  Index window{10};
  // Growth factor, finite and > 1.
  Real growthFactor{10.0};
  // No detection before this outer iteration.
  Index startIteration{10};

  bool operator==(const DivergenceDetectionSettings&) const = default;
};

struct AdaptiveRelaxationSettings {
  bool enabled{false};
  // Bounds, each in (0, 1] with min <= max. The configured
  // velocity/pressure relaxation is the INITIAL value and must lie within
  // its bounds when the controller is enabled.
  Real minVelocity{0.1};
  Real maxVelocity{0.9};
  Real minPressure{0.05};
  Real maxPressure{0.7};

  bool operator==(const AdaptiveRelaxationSettings&) const = default;
};

inline constexpr Index kMaxDetectionWindow = 10000;

struct SolverRobustnessSettings {
  ConvergenceCriterion convergenceCriterion{ConvergenceCriterion::Absolute};
  ResidualNormalizationSettings normalization;
  StagnationDetectionSettings stagnation;
  DivergenceDetectionSettings divergence;
  AdaptiveRelaxationSettings adaptiveRelaxation;
  cfd::algebra::LinearSolverFallbackSettings linearSolverFallback;

  bool operator==(const SolverRobustnessSettings&) const = default;
};

// Throws InvalidArgumentError (clear message naming the field) if any value
// is structurally invalid: referenceIterations == 0; a normalized tolerance
// not in (0, 1); a window < 2 or > kMaxDetectionWindow; a
// minRelativeImprovement not in (0, 1); growthFactor not finite or <= 1; a
// relaxation bound not in (0, 1] or min > max; or, with the adaptive
// controller enabled, an initial relaxation outside its bounds;
// linearSolverFallback.maxAttempts > kMaxLinearSolverFallbackAttempts.
// Validated whether or not a feature is enabled.
void validateSolverRobustnessSettings(const SolverRobustnessSettings& settings,
                                      Real initialVelocityRelaxation,
                                      Real initialPressureRelaxation);

// ---------------------------------------------------------------------------
// ResidualTracker
// ---------------------------------------------------------------------------

// One residual quantity's history.
//
// Normalization: normalized = latest / max(reference, floor), where
// `reference` is the largest FINITE value among the first
// `referenceIterations` pushes and `floor` > 0 is the quantity's absolute
// tolerance (the solvers pass it). The floor is the zero-baseline policy:
//   - a reference of exactly 0 (the quantity started at 0) or anything below
//     the floor is replaced by the floor, so normalized = latest / floor --
//     never a division by zero, never an infinite or meaningless enormous
//     value; it then reads "multiples of the absolute tolerance";
//   - a reference below the absolute tolerance carries no information the
//     absolute gate does not already have.
// For finite input every derived value is finite (a ratio that would
// overflow, e.g. 1e300 / 1e-12, saturates at the largest finite double). A
// non-finite push is
// recorded as `latest` (normalized() is then non-finite, flagged by
// latestFinite()/nonFiniteSeen()) but never enters reference, best, or the
// window statistics.
//
// Window: the last `windowCapacity` finite values in a fixed ring buffer --
// O(windowCapacity) memory, O(1) push, O(windowCapacity) window statistics.
// bestBeforeWindow(): the smallest finite value that has already LEFT the
// window and whose 1-based push index is >= `bestFrom` (so detection logic
// can exclude a startup transient), +infinity if none.
class ResidualTracker {
 public:
  ResidualTracker(Real floor, Index referenceIterations, Index windowCapacity, Index bestFrom = 1);

  void push(Real value);

  [[nodiscard]] Index count() const noexcept { return count_; }
  [[nodiscard]] Real latest() const noexcept { return latest_; }
  [[nodiscard]] bool latestFinite() const noexcept;
  [[nodiscard]] bool nonFiniteSeen() const noexcept { return nonFiniteSeen_; }
  [[nodiscard]] Real reference() const noexcept { return reference_; }
  [[nodiscard]] Real floor() const noexcept { return floor_; }
  [[nodiscard]] Real effectiveReference() const noexcept;
  [[nodiscard]] Real normalized() const noexcept;
  [[nodiscard]] Real best() const noexcept { return best_; }

  [[nodiscard]] Index windowCapacity() const noexcept { return capacity_; }
  [[nodiscard]] Index windowSize() const noexcept { return size_; }
  // Over the current window (requires windowSize() > 0).
  [[nodiscard]] Real windowMin() const;
  [[nodiscard]] Real windowMax() const;
  [[nodiscard]] Real windowMean() const;
  // 0 = the most recent finite value; requires ago < windowSize().
  [[nodiscard]] Real valueAgo(Index ago) const;
  [[nodiscard]] Real bestBeforeWindow() const noexcept { return bestBeforeWindow_; }

  // value / max(reference, floor) -- the normalization rule above.
  [[nodiscard]] static Real normalize(Real value, Real reference, Real floor) noexcept;

 private:
  Real floor_;
  Index referenceIterations_;
  Index capacity_;
  Index bestFrom_;
  Index count_{0};
  Index finiteCount_{0};
  Real latest_{0.0};
  bool nonFiniteSeen_{false};
  Real reference_{0.0};
  Real best_;
  Real bestBeforeWindow_;
  std::vector<Real> ring_;
  std::vector<Index> ringIndex_;
  Index head_{0};  // slot of the next write
  Index size_{0};
};

// ---------------------------------------------------------------------------
// AdaptiveRelaxationController
// ---------------------------------------------------------------------------

struct RelaxationFactors {
  Real velocity{};
  Real pressure{};
};

enum class RelaxationAction {
  Hold,
  Increase,
  Decrease,
};

// The one adaptive under-relaxation controller. update() is called exactly
// once per COMPLETED outer iteration with that iteration's residual measure
// m_n (the solvers pass max of the normalized u, v, p residuals); the
// factors it returns are used for the whole NEXT outer iteration -- never
// changed inside an iteration. Rules, in order (n = number of updates):
//   n == 1                                   -> Hold
//   growth: m_n > kGrowthRatio * m_(n-1)     -> Decrease
//   growing oscillation: the last kOscillationRatios step ratios alternate
//     strictly above/below 1 AND m_n >= m_(n-2) (the envelope is not
//     decaying)                              -> Decrease
//   m_n < m_(n-1): improvement streak += 1; after kImprovementStreak
//     consecutive improvements               -> Increase (streak reset; a
//     Hold if both factors already sit at their max/ceiling)
//   otherwise (flat or mild rise)            -> Hold (streak reset)
// Decrease: alpha <- max(min, kDecreaseFactor * alpha), and the factor that
// just proved unstable is remembered as a ceiling, ceiling <- max(min,
// kCeilingFactor * alpha_before) (ceilings only ever go down); Increase:
// alpha <- min(max, ceiling, kIncreaseFactor * alpha). Without the ceiling
// the controller "hunts" -- climbing back into the unstable range after
// every recovery (measured: ~7 increases per decrease on the aggressive
// cavity, see the evidence). Velocity and pressure scale together, each
// clamped to its own bounds, so 0 < alpha <= 1 always. A non-finite
// measure is a Hold. Disabled: always Hold, factors fixed at the
// initial values (bit-identical to fixed relaxation). Deterministic: the
// factors depend only on the sequence of measures.
// The constants are EMPIRICAL (chosen for gradual, bounded changes: +5% per
// increase at most once every 5 iterations, -30% per decrease), documented
// in results/p12-num-004/summary.md together with the controlled cases
// they were exercised on.
class AdaptiveRelaxationController {
 public:
  static constexpr Real kIncreaseFactor = 1.05;
  static constexpr Real kDecreaseFactor = 0.7;
  static constexpr Real kGrowthRatio = 1.2;
  static constexpr Index kImprovementStreak = 5;
  static constexpr Index kOscillationRatios = 4;
  static constexpr Real kCeilingFactor = 0.9;

  AdaptiveRelaxationController(AdaptiveRelaxationSettings settings, Real initialVelocity,
                               Real initialPressure);

  RelaxationAction update(Real measure);

  [[nodiscard]] RelaxationFactors current() const noexcept { return factors_; }
  [[nodiscard]] Index increases() const noexcept { return increases_; }
  [[nodiscard]] Index decreases() const noexcept { return decreases_; }

 private:
  AdaptiveRelaxationSettings settings_;
  RelaxationFactors factors_;
  RelaxationFactors ceiling_;
  // The last kOscillationRatios + 1 measures (oldest first), fixed size.
  std::array<Real, kOscillationRatios + 1> recent_{};
  Index count_{0};
  Index streak_{0};
  Index increases_{0};
  Index decreases_{0};
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

inline constexpr Index kMaxRecordedFallbackEvents = 32;

struct LinearSolverFallbackEvent {
  Index iteration{0};  // 1-based outer iteration
  std::string equation;
  cfd::algebra::LinearSolverFallbackReport report;
};

// Everything P12-NUM-004 adds to a SIMPLE/CompressibleSIMPLE result. Every
// history has one entry per COMPLETED outer iteration, like the absolute
// residual histories.
struct OuterIterationDiagnostics {
  ConvergenceCriterion convergenceCriterion{ConvergenceCriterion::Absolute};

  std::vector<Real> uNormalizedHistory;
  std::vector<Real> vNormalizedHistory;
  std::vector<Real> pressureNormalizedHistory;
  std::vector<Real> continuityNormalizedHistory;
  // Effective normalization references (max(reference, floor)) at the end.
  Real uReference{};
  Real vReference{};
  Real pressureReference{};
  Real continuityReference{};
  // P12-MESH-006: the W residual's normalized history and reference (3D
  // solves only; empty / 0 when the samples carry no w).
  std::vector<Real> wNormalizedHistory;
  Real wReference{};
  // max over all gates of value / threshold; <= 1 on every gate means
  // converged. One entry per completed iteration.
  std::vector<Real> convergenceDistanceHistory;

  // The relaxation factors USED in each completed outer iteration.
  std::vector<Real> velocityRelaxationHistory;
  std::vector<Real> pressureRelaxationHistory;
  Index relaxationIncreases{0};
  Index relaxationDecreases{0};

  // Linear solves that went through the fallback policy (primary failed
  // with an eligible status), and how many of those recovered.
  Index linearSolverFallbacks{0};
  Index linearSolverFallbackRecoveries{0};
  // The first kMaxRecordedFallbackEvents such solves, in order.
  std::vector<LinearSolverFallbackEvent> fallbackEvents;

  // Human-readable reason for a Stagnated/Diverging status, or a failed
  // linear solve; empty otherwise.
  std::string statusDetail;
};

// Records `report` (if report.attempted) against `diagnostics`.
void recordLinearSolverFallback(OuterIterationDiagnostics& diagnostics, Index iteration,
                                std::string_view equation,
                                const cfd::algebra::LinearSolverFallbackReport& report);

// "<equation> linear solve failed: <type> <status>[, fallback <type> <status>...]".
[[nodiscard]] std::string describeLinearSolveFailure(std::string_view equation,
                                                     cfd::algebra::LinearSolverType primaryType,
                                                     const cfd::algebra::SolverResult& result);

// ---------------------------------------------------------------------------
// OuterIterationMonitor
// ---------------------------------------------------------------------------

struct OuterResidualSample {
  Real u{};
  Real v{};
  Real pressure{};
  Real continuity{};
  Real globalImbalance{};
  std::optional<Real> turbulence;
  // P12-MESH-006: the W momentum residual of a 3D solve (absent in 2D). When
  // present it is gated exactly like u and v (velocity tolerance), and joins
  // the convergence distance, the divergence check and the relaxation
  // controller's input; absent, every decision is exactly the 2D one.
  std::optional<Real> w{};
};

struct OuterConvergenceTolerances {
  Real velocity{};
  Real pressure{};
  Real continuity{};
  Real turbulence{};
};

enum class OuterIterationVerdict {
  Continue,
  Converged,
  Stagnated,
  Diverging,
};

// Called once per completed outer iteration (record()), in this order:
//
// 1. Convergence.
//    Absolute:   u, v <= velocity tol; p <= pressure tol; continuity and
//                global imbalance <= continuity tol; turbulence (if the
//                model reports one) <= turbulence tol -- exactly the
//                pre-P12-NUM-004 comparisons.
//    Normalized: u <= max(nvTol * ref_u, velocity tol) (same for v; p with
//                npTol and the pressure tol) -- i.e. normalized <= tolerance
//                OR already below the absolute tolerance, so a tiny
//                arbitrary absolute scale never blocks convergence and a
//                tiny/zero reference never makes it easier (references are
//                floored at the absolute tolerance). Continuity, global mass
//                imbalance and turbulence keep their ABSOLUTE gates, and
//                every residual must be finite -- a small normalized
//                momentum residual alone never converges a solve whose
//                conservation state is unacceptable.
// 2. Divergence (if enabled, n >= startIteration + window), per quantity q
//    in {u, v, p, continuity}, with W = window, G = growthFactor and
//    B = max(floor_q, smallest value of q since startIteration that is
//    older than the window):
//      persistent excursion: every one of the last W values >= G * B, or
//      sustained runaway:    the last W values strictly increase and the
//                            newest >= G * the oldest of them.
//    Either -> Diverging. W consecutive iterations are always required, so
//    a single spike never triggers. In addition, a residual NORM that
//    overflowed to a non-finite value (the fields themselves are checked by
//    the solvers -> NonFiniteState) is Diverging at once -- the windowed
//    criteria cannot see it because trackers exclude non-finite values.
// 3. Stagnation (if enabled, n >= startIteration, n > W): with the
//    convergence distance D_n = max over gates of value / threshold
//    (converged iff every gate <= 1), best_old = min D before the last W
//    iterations, best_new = min D within them:
//      improvement = (best_old - best_new) / best_old
//      improvement < minRelativeImprovement -> Stagnated.
//    A solve contracting D by a factor rho per iteration has improvement
//    1 - rho^W, so it is "slow" (not stagnating) as long as
//    rho < (1 - minRelativeImprovement)^(1/W) -- e.g. rho < 0.99980 for the
//    defaults W = 50, 1%. Divergence is checked first; a growth too slow to
//    meet the divergence criterion is reported as Stagnated (no progress).
// 4. Relaxation update (if nothing above ended the solve): the adaptive
//    controller is fed max(normalized u, v, p).
class OuterIterationMonitor {
 public:
  OuterIterationMonitor(const SolverRobustnessSettings& settings,
                        OuterConvergenceTolerances tolerances, Real initialVelocityRelaxation,
                        Real initialPressureRelaxation);

  OuterIterationVerdict record(const OuterResidualSample& sample);

  // The factors to use for the next outer iteration.
  [[nodiscard]] RelaxationFactors relaxation() const noexcept { return controller_.current(); }
  [[nodiscard]] RelaxationAction lastRelaxationAction() const noexcept { return lastAction_; }
  [[nodiscard]] const ResidualTracker& u() const noexcept { return u_; }
  [[nodiscard]] const ResidualTracker& v() const noexcept { return v_; }
  [[nodiscard]] const ResidualTracker& w() const noexcept { return w_; }
  [[nodiscard]] const ResidualTracker& pressure() const noexcept { return p_; }
  [[nodiscard]] const ResidualTracker& continuity() const noexcept { return c_; }
  [[nodiscard]] Real convergenceDistance() const noexcept { return distance_; }

  [[nodiscard]] OuterIterationDiagnostics& diagnostics() noexcept { return diagnostics_; }
  [[nodiscard]] OuterIterationDiagnostics takeDiagnostics();

 private:
  [[nodiscard]] bool isConverged(const OuterResidualSample& sample) const;
  [[nodiscard]] Real threshold(const ResidualTracker& tracker, Real normalizedTolerance) const;
  [[nodiscard]] bool diverging(const ResidualTracker& tracker, std::string_view name);

  SolverRobustnessSettings settings_;
  OuterConvergenceTolerances tolerances_;
  ResidualTracker u_;
  ResidualTracker v_;
  ResidualTracker p_;
  ResidualTracker c_;
  ResidualTracker w_;  // P12-MESH-006: fed only by samples that carry w.
  ResidualTracker distanceTracker_;
  AdaptiveRelaxationController controller_;
  RelaxationAction lastAction_{RelaxationAction::Hold};
  Real distance_{0.0};
  OuterIterationDiagnostics diagnostics_;
};

}  // namespace cfd::solver
