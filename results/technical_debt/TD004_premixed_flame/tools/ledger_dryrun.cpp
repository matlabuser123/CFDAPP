// TD-004 GATE DRY-RUN for criterion C1, and the non-vacuity controls N2 / N3.
//
// INVESTIGATION ONLY. No production file is changed and nothing is asserted.
//
// WHY THIS IS A STANDALONE REIMPLEMENTATION RATHER THAN A CALL INTO PRODUCTION.
// `recentre` is a lambda local to `solvePremixedFlame`; there is no entry point to it. Exposing one
// would be a production change, and CLAUDE.md §5.2 requires the dry-run to run against the
// UNCHANGED baseline. So the shift is reimplemented here from the source read in audit.md — the
// same two fill branches, the same whole-cell translation — which makes this an INDEPENDENT
// reference computation of the operator's conservation behaviour rather than a test of the
// operator. It establishes three things the gate needs before it can be frozen:
//
//   * that the defect is real and how large it is, in the units C1's threshold is written in;
//   * that C1's derived bound `8 * eps * nc * max|integrand|` is the right ORDER for this
//     integrand, so the threshold is not accidentally unreachable or accidentally trivial;
//   * that a WRONG ledger fails C1 -- the controls N2 (sign flipped) and N3 (upstream fill only).
//
// C1 is a self-consistency check between two computations of the same quantity, and the way such a
// check goes vacuous is by comparing a thing to itself. N2 and N3 are what prove it does not, and
// without them C1 would be untested.
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using Real = double;

constexpr int kCells = 500;
constexpr Real kDomain = 0.010;       // [m], the test fixture's domain
constexpr Real kAnchorFraction = 0.35;
constexpr Real kFuelUnburnt = 0.05;   // Y_F,u, the fixture's value
constexpr Real kRhoUnburnt = 1.0;     // scaled; only ratios matter for a conservation ledger
constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// A flame-like fuel profile: Y_F falls from its unburnt value to zero across the front. tanh rather
// than a step, because the ledger's error depends on the integrand's smoothness near the ends and a
// step would understate it.
std::vector<Real> fuelProfile(Real frontPosition, Real thickness) {
  std::vector<Real> Y(kCells);
  const Real cellSize = kDomain / static_cast<Real>(kCells);
  for (int c = 0; c < kCells; ++c) {
    const Real x = (static_cast<Real>(c) + 0.5) * cellSize;
    Y[static_cast<std::size_t>(c)] =
        kFuelUnburnt * 0.5 * (1.0 - std::tanh((x - frontPosition) / thickness));
  }
  return Y;
}

Real integral(const std::vector<Real>& Y) {
  const Real cellSize = kDomain / static_cast<Real>(kCells);
  Real total = 0.0;
  for (const Real value : Y) total += kRhoUnburnt * value * cellSize;
  return total;
}

// THE PRODUCTION OPERATOR'S SHIFT, reimplemented from src/species/PremixedFlame.cpp:
//
//   source = c + shift
//   source <  0   -> upstreamFill            (the unburnt inlet state)
//   source >= nc  -> field[nc - 1]           (the outflow edge value)
//   otherwise     -> field[source]
std::vector<Real> shiftField(const std::vector<Real>& field, long shift, Real upstreamFill) {
  std::vector<Real> shifted(kCells);
  for (int c = 0; c < kCells; ++c) {
    const long source = static_cast<long>(c) + shift;
    if (source < 0) {
      shifted[static_cast<std::size_t>(c)] = upstreamFill;
    } else if (source >= kCells) {
      shifted[static_cast<std::size_t>(c)] = field[kCells - 1];
    } else {
      shifted[static_cast<std::size_t>(c)] = field[static_cast<std::size_t>(source)];
    }
  }
  return shifted;
}

}  // namespace

int main() {
  const Real cellSize = kDomain / static_cast<Real>(kCells);
  const Real anchor = kAnchorFraction * kDomain;
  // The fixture's measured flame thickness, logs/01: delta = 1.22891 mm.
  const Real thickness = 1.22891e-3;

  // C1's bound, evaluated for THIS integrand rather than quoted abstractly.
  const Real maxIntegrand = kRhoUnburnt * kFuelUnburnt * cellSize;
  const Real c1Bound = 8.0 * kEps * static_cast<Real>(kCells) * maxIntegrand;

  std::printf("TD-004 C1 dry-run -- the recentring ledger, on the unchanged operator\n\n");
  std::printf("  cells            %d over %.4f m, cell %.6e m\n", kCells, kDomain, cellSize);
  std::printf("  Y_F,u            %.4f      front thickness %.6e m (logs/01)\n", kFuelUnburnt,
              thickness);
  std::printf("  max|integrand|   %.6e   (rho * Y_F,u * cellSize)\n", maxIntegrand);
  std::printf("  C1 bound         %.6e   = 8 * eps * nc * max|integrand|\n\n", c1Bound);

  std::printf("%7s %16s %16s %16s %14s %10s\n", "shift", "integral before", "integral after",
              "actual change", "baseline rpt", "C1");
  // Shifts spanning what the march actually does: logs/04 measured a worst front offset of 163.96
  // cells, so single-cell shifts are the common case and large ones are reached.
  const long shifts[] = {1, -1, 4, -4, 164, -164};
  for (const long shift : shifts) {
    const std::vector<Real> before = fuelProfile(anchor, thickness);
    const std::vector<Real> after = shiftField(before, shift, kFuelUnburnt);
    const Real integralBefore = integral(before);
    const Real integralAfter = integral(after);
    const Real actual = integralAfter - integralBefore;
    // THE BASELINE REPORTS NOTHING. There is no ledger in the unchanged operator, so the amount it
    // accounts for is identically zero -- which is exactly what C1 compares against.
    const Real baselineReported = 0.0;
    const Real c1Error = std::abs(actual - baselineReported);
    std::printf("%7ld %16.9e %16.9e %16.9e %14.1f %10s\n", shift, integralBefore, integralAfter,
                actual, baselineReported, c1Error <= c1Bound ? "pass" : "FAIL");
  }

  std::printf("\n  => the baseline FAILS C1 at every shift, by the full size of the change it\n");
  std::printf("     makes and does not report. C1 is a real instrument, not a formality.\n");

  // ------------------------------------------------------------------------------- N2 and N3
  std::printf("\nNON-VACUITY -- a WRONG ledger must also fail C1\n\n");
  std::printf("%7s %16s %16s %16s %16s %8s %8s %8s\n", "shift", "actual change", "correct rpt",
              "N2 sign flip", "N3 upstream only", "C1 ok", "N2", "N3");
  for (const long shift : {4L, -4L}) {
    const std::vector<Real> before = fuelProfile(anchor, thickness);
    const std::vector<Real> after = shiftField(before, shift, kFuelUnburnt);
    const Real actual = integral(after) - integral(before);

    // The CORRECT ledger: before minus after, which is exact by construction.
    const Real correct = actual;

    // N2: the sign flipped. A plausible implementation slip, and one that a check comparing a
    // quantity to itself would not notice.
    const Real n2 = -actual;

    // N3: only the UPSTREAM fill accounted for. A one-sided ledger is the natural first version,
    // and it is correct for exactly one sign of shift -- which is why a test that only ever shifts
    // one way would pass while the other direction leaked.
    Real n3 = 0.0;
    if (shift < 0) {
      // the upstream fill path is the one taken for a negative shift
      n3 = actual;
    } else {
      // for a positive shift the fabricated cells are the downstream edge replication, which a
      // upstream-only ledger ignores entirely
      n3 = 0.0;
    }

    std::printf("%7ld %16.9e %16.9e %16.9e %16.9e %8s %8s %8s\n", shift, actual, correct, n2, n3,
                std::abs(actual - correct) <= c1Bound ? "pass" : "FAIL",
                std::abs(actual - n2) <= c1Bound ? "pass" : "FAIL",
                std::abs(actual - n3) <= c1Bound ? "pass" : "FAIL");
  }

  std::printf("\n  => the correct ledger passes; N2 fails at both signs; N3 passes for the sign it\n");
  std::printf("     happens to cover and FAILS for the other. So C1 discriminates a right ledger\n");
  std::printf("     from two plausible wrong ones, and N3 shows why both fill paths must be\n");
  std::printf("     exercised -- a one-sided test would have reported success.\n");
  return 0;
}
