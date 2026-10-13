#pragma once

// P17.9 -- wave detectors and the exact Sod sampler, for shock validation.
//
// TEST-SIDE ON PURPOSE. The authorization's §3 says an exact Riemann solver written for validation
// must be "isolated under test/validation infrastructure rather than production solver code", and
// gate criterion A3 requires P17.9 to add NO production source. Nothing in this file is reachable
// from the solver.
//
// THE ALGORITHMS ARE FROZEN IN results/p17/shock_validation/acceptance_gate.md §2 and §3, and they
// were DRY-RUN IN PYTHON BEFORE THE GATE WAS FROZEN
// (results/p17/shock_validation/tools/dry_run_detectors.py, logs/01_dryrun.log). That dry-run found
// three defects in these very algorithms, and it is worth recording what they were, because each
// one would have produced a confident wrong number rather than an obvious failure:
//
//   1. the fan detector selected every cell differing from BOTH plateaus, which swept in the
//      star-RIGHT region between the contact and the shock. The fan measured 147 cells against an
//      exact 44.5 and the tail was wrong by 102 dx.
//        -> the fan search is CONFINED to x < the detected contact position.
//   2. the shock detector returned a position on a smooth monotone ramp containing no jump at all,
//      because a 50 % crossing exists on any monotone profile.
//        -> a crossing is accepted only when it joins two PLATEAUS.
//   3. the fan TAIL was defined as the last departure from the star plateau, which the CONTACT's
//      smearing pushed 40 dx to the right.
//        -> the tail is the FIRST ARRIVAL at the star plateau, scanning forward from the head.
//
// A test in the P17.9 suite re-runs the Python dry-run's synthetic cases through THIS code and
// requires the same answers, so the two implementations cannot drift apart.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "shock_reference_data.hpp"

namespace cfd::shock_detect {

struct Profile {
  std::vector<double> x;
  std::vector<double> density;
  std::vector<double> velocity;
  std::vector<double> pressure;

  [[nodiscard]] std::size_t size() const noexcept { return x.size(); }
};

struct ExactState {
  double density;
  double velocity;
  double pressure;
};

// ---------------------------------------------------------------------------------------------
// the exact Sod solution at a ray xi = x/t
// ---------------------------------------------------------------------------------------------
//
// The STAR STATE and the WAVE SPEEDS are embedded from the 60-digit Python solve; what happens here
// is only the algebraic self-similar sampling. The split is deliberate: the nonlinear Newton solve
// for p* -- the part that can be subtly wrong -- stays in the frozen reference, and 41 spot points
// of the Python profile are embedded so this sampler is checked rather than trusted.
[[nodiscard]] inline ExactState sampleExactSod(double xi) {
  namespace R = cfd::shock_reference;
  if (xi <= R::kSodRarefactionHeadSpeed) {
    return {R::kSodLeftDensity, R::kSodLeftVelocity, R::kSodLeftPressure};
  }
  if (xi < R::kSodRarefactionTailSpeed) {
    // inside the left fan (Toro eq. 4.56), with u_L = 0
    const double g = R::kGamma;
    const double aL = R::kSodSoundSpeedLeft;
    const double u = 2.0 / (g + 1.0) * (aL + (g - 1.0) / 2.0 * R::kSodLeftVelocity + xi);
    const double a = 2.0 / (g + 1.0) * (aL + (g - 1.0) / 2.0 * (R::kSodLeftVelocity - xi));
    const double rho = R::kSodLeftDensity * std::pow(a / aL, 2.0 / (g - 1.0));
    const double p = R::kSodLeftPressure * std::pow(a / aL, 2.0 * g / (g - 1.0));
    return {rho, u, p};
  }
  if (xi < R::kSodContactSpeed) {
    return {R::kSodRhoStarLeft, R::kSodUStar, R::kSodPStar};
  }
  if (xi < R::kSodShockSpeed) {
    return {R::kSodRhoStarRight, R::kSodUStar, R::kSodPStar};
  }
  return {R::kSodRightDensity, R::kSodRightVelocity, R::kSodRightPressure};
}

[[nodiscard]] inline ExactState sampleExactSodAt(double x, double t) {
  return sampleExactSod((x - cfd::shock_reference::kSodDiaphragm) / t);
}

// ---------------------------------------------------------------------------------------------
// detector 1 -- the 50 % crossing of a JUMP between two plateaus
// ---------------------------------------------------------------------------------------------
//
// Returns nullopt when there is no jump. That is how "this wave is not present" is reported; a
// detector that always returns a plausible number would make every position criterion vacuous, and
// the dry-run's control V4 proved that is not hypothetical.
//
// A crossing is accepted only when a FLAT window sitting at each plateau value can be found on the
// corresponding side -- see hasFlatWindowToward. `plateauGap` is where the outward search starts,
// not where the plateau is required to be.
// Is there a FLAT window sitting at `value` somewhere on one side of index `i`?
//
// "Flat" is two conditions, and the second is the one that matters: the window's mean must be
// within `tol` of `value`, AND its own max-min spread must also be within `tol`. A monotone ramp
// passes through every value on its way, so a mean test alone accepts it -- the SPREAD test is what
// distinguishes a plateau from a slope that happens to be crossing.
//
// The search walks OUTWARD rather than testing one fixed offset. The first version tested a window
// at exactly 5 cells and rejected a perfectly correct Sod contact, because a first-order contact is
// linearly degenerate and smears over ~sqrt(n_steps) cells -- about 30 at nx = 200 after 677 steps.
// Requiring the plateau to begin within 5 cells of the crossing was a statement about smearing, not
// about whether a plateau exists. Walking outward asks the question the gate's criterion W5
// actually poses: away from the waves, is the profile flat?
[[nodiscard]] inline bool hasFlatWindowToward(const std::vector<double>& qs, std::size_t i,
                                              int direction, double value, double tol, int minGap,
                                              int span) {
  const long n = static_cast<long>(qs.size());
  const long maxSearch = n / 4;  // a plateau further than a quarter of the domain away is not one
  for (long gap = minGap; gap <= maxSearch; ++gap) {
    const long start = static_cast<long>(i) + direction * gap;
    const long stop = start + direction * (span - 1);
    const long lo = std::min(start, stop);
    const long hi = std::max(start, stop);
    if (lo < 0 || hi >= n) return false;
    double sum = 0.0;
    double mn = qs[static_cast<std::size_t>(lo)];
    double mx = mn;
    for (long k = lo; k <= hi; ++k) {
      const double v = qs[static_cast<std::size_t>(k)];
      sum += v;
      mn = std::min(mn, v);
      mx = std::max(mx, v);
    }
    const double mean = sum / static_cast<double>(hi - lo + 1);
    if (std::abs(mean - value) <= tol && (mx - mn) <= tol) return true;
  }
  return false;
}

[[nodiscard]] inline std::optional<double> crossingPosition(
    const std::vector<double>& xs, const std::vector<double>& qs, double qPre, double qPost,
    bool requirePlateaus = true, int plateauGap = 5, int plateauSpan = 5) {
  if (xs.size() < 2 || xs.size() != qs.size()) return std::nullopt;
  const double mid = 0.5 * (qPre + qPost);
  const bool rising = qPost > qPre;
  const double jump = std::abs(qPost - qPre);
  if (jump == 0.0) return std::nullopt;
  const double tol = jump / 10.0;

  for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
    const double a = qs[i];
    const double b = qs[i + 1];
    if (a == b) continue;
    const double lo = std::min(a, b);
    const double hi = std::max(a, b);
    if (mid < lo || mid > hi) continue;
    if (rising ? !(b > a) : !(b < a)) continue;

    if (requirePlateaus) {
      if (!hasFlatWindowToward(qs, i, -1, qPre, tol, plateauGap, plateauSpan) ||
          !hasFlatWindowToward(qs, i, +1, qPost, tol, plateauGap, plateauSpan)) {
        return std::nullopt;
      }
    }

    const double t = (mid - a) / (b - a);
    return xs[i] + t * (xs[i + 1] - xs[i]);
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// detector 2 -- the rarefaction fan's head and tail
// ---------------------------------------------------------------------------------------------

struct FanExtent {
  std::optional<double> head;
  std::optional<double> tail;
  int cells{0};
};

[[nodiscard]] inline FanExtent fanExtent(const std::vector<double>& xs,
                                         const std::vector<double>& rhos, double xMax,
                                         double rhoOuter, double rhoInner, double frac = 0.01) {
  FanExtent out;
  const double drop = std::abs(rhoOuter - rhoInner);
  if (drop == 0.0 || xs.size() < 3) return out;
  const double tol = frac * drop;

  std::vector<std::size_t> idx;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    if (xs[i] < xMax) idx.push_back(i);
  }
  if (idx.size() < 3) return out;

  std::optional<std::size_t> headI;
  for (const std::size_t i : idx) {
    if (std::abs(rhos[i] - rhoOuter) > tol) {
      headI = i;
      break;
    }
  }
  if (!headI) return out;

  // FIRST ARRIVAL at the inner plateau, scanning forward -- not the last departure scanning back.
  // The backward form was wrong by 40 dx because the contact's smearing bleeds left into the
  // star-left plateau; a first arrival cannot be affected by anything further right.
  std::optional<std::size_t> tailI;
  for (const std::size_t i : idx) {
    if (i > *headI && std::abs(rhos[i] - rhoInner) <= tol) {
      tailI = i;
      break;
    }
  }
  if (!tailI || *tailI < *headI) return out;

  double head = xs[*headI];
  if (*headI > 0) {
    const double span = std::abs(rhos[*headI] - rhos[*headI - 1]);
    if (span > 0.0) {
      const double back = (std::abs(rhos[*headI] - rhoOuter) - tol) / span;
      head = xs[*headI] - (xs[*headI] - xs[*headI - 1]) * std::clamp(back, 0.0, 1.0);
    }
  }
  double tail = xs[*tailI];
  if (*tailI > 0) {
    const double span = std::abs(rhos[*tailI] - rhos[*tailI - 1]);
    if (span > 0.0) {
      const double back = (tol - std::abs(rhos[*tailI] - rhoInner)) / span;
      tail = xs[*tailI] - (xs[*tailI] - xs[*tailI - 1]) * std::clamp(back, 0.0, 1.0);
    }
  }
  out.head = head;
  out.tail = tail;
  out.cells = static_cast<int>(*tailI - *headI + 1);
  return out;
}

// ---------------------------------------------------------------------------------------------
// detector 3 -- the plateau sampling policy (authorization §27)
// ---------------------------------------------------------------------------------------------

struct PlateauSample {
  std::optional<double> mean;
  int cells{0};
};

// Mean of q over (xLo, xHi) with `exclude` cells trimmed from EACH end. Fewer than 5 remaining
// cells means the window is TOO NARROW and the caller must FAIL the criterion -- never widen it.
[[nodiscard]] inline PlateauSample plateauMean(const std::vector<double>& xs,
                                               const std::vector<double>& qs, double xLo,
                                               double xHi, double dx, int exclude = 5) {
  PlateauSample out;
  const double lo = xLo + exclude * dx;
  const double hi = xHi - exclude * dx;
  double sum = 0.0;
  int n = 0;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    if (xs[i] >= lo && xs[i] <= hi) {
      sum += qs[i];
      ++n;
    }
  }
  out.cells = n;
  if (n >= 5) out.mean = sum / n;
  return out;
}

// ---------------------------------------------------------------------------------------------
// detector 4 -- the UNCONTAMINATED plateau estimator (AMENDMENT A3)
// ---------------------------------------------------------------------------------------------
//
// plateauMean() above averages a window with a FIXED exclusion from each bounding wave. For the
// pressure and velocity that is fine. For the DENSITY between a contact and a shock it is not, and
// the reason is physical rather than a tuning detail:
//
//   a contact carries a density jump but NO pressure or velocity jump. Its diffusive tail -- which
//   spreads like sqrt(n_steps) while a fixed exclusion stays at 5 cells -- therefore corrupts a
//   density measurement while leaving p and u untouched.
//
// That is not asserted, it is measured: under the fixed policy p* converges at rate 1.37 while
// rho*_R does not converge at all (rate -0.045), from the SAME window of the SAME run.
// See results/p17/shock_validation/amendments/A3_plateau_contamination.md.
//
// This estimator takes the mean of the `span`-cell sub-window with the SMALLEST max-min spread
// between the two waves. The flattest sub-window is by construction the one least influenced by
// either wave's tail, so it isolates the plateau instead of averaging the contamination in. It is
// still fully sensitive to the physics: if the solver's plateau value were wrong, the flattest
// window would faithfully report the wrong value.
struct FlatPlateau {
  std::optional<double> mean;
  double spread{};
  double centre{};
};

[[nodiscard]] inline FlatPlateau flattestPlateau(const std::vector<double>& xs,
                                                 const std::vector<double>& qs, double xLo,
                                                 double xHi, double dx, int span = 9, int gap = 2) {
  FlatPlateau out;
  std::vector<std::size_t> idx;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    if (xs[i] >= xLo + gap * dx && xs[i] <= xHi - gap * dx) idx.push_back(i);
  }
  if (static_cast<int>(idx.size()) < span) return out;
  double best = 0.0;
  double bestSpread = -1.0;
  double bestCentre = 0.0;
  for (std::size_t s = 0; s + static_cast<std::size_t>(span) <= idx.size(); ++s) {
    double mn = qs[idx[s]];
    double mx = mn;
    double sum = 0.0;
    for (int k = 0; k < span; ++k) {
      const double v = qs[idx[s + static_cast<std::size_t>(k)]];
      mn = std::min(mn, v);
      mx = std::max(mx, v);
      sum += v;
    }
    const double spread = mx - mn;
    if (bestSpread < 0.0 || spread < bestSpread) {
      bestSpread = spread;
      best = sum / span;
      bestCentre = xs[idx[s + static_cast<std::size_t>(span / 2)]];
    }
  }
  if (bestSpread >= 0.0) {
    out.mean = best;
    out.spread = bestSpread;
    out.centre = bestCentre;
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
// field errors
// ---------------------------------------------------------------------------------------------

struct FieldError {
  double l1{};
  double l2{};
  double linf{};
};

[[nodiscard]] inline FieldError fieldError(const std::vector<double>& numerical,
                                           const std::vector<double>& exact,
                                           const std::vector<double>& volume) {
  FieldError e;
  double totalVolume = 0.0;
  for (std::size_t i = 0; i < numerical.size(); ++i) {
    const double d = std::abs(numerical[i] - exact[i]);
    e.l1 += d * volume[i];
    e.l2 += d * d * volume[i];
    e.linf = std::max(e.linf, d);
    totalVolume += volume[i];
  }
  if (totalVolume > 0.0) {
    e.l1 /= totalVolume;
    e.l2 = std::sqrt(e.l2 / totalVolume);
  }
  return e;
}

// The smeared width of a transition, in cells: how many cells lie strictly between 10 % and 90 %
// of the jump. Reported as solver characterisation (authorization §23), not gated.
[[nodiscard]] inline int transitionWidthCells(const std::vector<double>& qs, double qPre,
                                              double qPost) {
  const double lo10 = qPre + (qPost - qPre) * 0.1;
  const double hi90 = qPre + (qPost - qPre) * 0.9;
  const double lo = std::min(lo10, hi90);
  const double hi = std::max(lo10, hi90);
  int n = 0;
  for (const double v : qs) {
    if (v >= lo && v <= hi) ++n;
  }
  return n;
}

}  // namespace cfd::shock_detect
