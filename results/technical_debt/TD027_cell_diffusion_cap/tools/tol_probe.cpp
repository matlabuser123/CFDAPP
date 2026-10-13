// TD-027 / production tolerance -- the GATE's measurements. Two jobs, both cheap, no flame solve.
//
//  1. REPORT the default that PremixedFlameSettings actually carries, so the change is observed
//     rather than asserted from the header text.
//  2. NON-VACUITY for the gate's C1. C1 claims "no caller reads the default, so changing it cannot
//     change behaviour". That claim is only meaningful if the default WOULD be read when a caller
//     omits the assignment. This probe constructs settings WITHOUT assigning tolerance and shows
//     the value that reaches the solver's own validate/bisect path, proving the default is live
//     and that C1's enumeration is load-bearing rather than vacuous.
//
// It deliberately does NOT solve a flame: the change under test is a DEAD-WRITTEN default, and the
// premixed-flame suite costs hours. That scope limit is stated in the gate.
#include <cmath>
#include <cstdio>

#include "cfd/species/PremixedFlame.hpp"

using cfd::species::PremixedFlameSettings;

namespace {

// The terminal bisection bracket. PremixedFlame.cpp:1832 bisects on the GEOMETRIC midpoint from an
// initial ratio high/low = 2, so after k bisections the bracket is 2^(1/2^k) - 1, and the loop
// stops at the first k with that <= tolerance. Derived in audit_C3.md §2; confirmed by the A4
// dry-run, which used 13 bisections and published 8.461627e-05 at tolerance 1e-4.
int terminalBracket(double tolerance, double* bracket) {
  int k = 0;
  double q = 1.0;  // k = 0: 2^1 - 1
  while (q > tolerance) {
    ++k;
    q = std::pow(2.0, std::ldexp(1.0, -k)) - 1.0;  // 2^(1/2^k) - 1
  }
  *bracket = q;
  return k;
}

}  // namespace

int main() {
  const PremixedFlameSettings fresh;
  std::printf("declared default tolerance            %.6e\n", fresh.tolerance);

  // NON-VACUITY: a caller that omits the assignment carries the default forward, so it would reach
  // settings.tolerance at PremixedFlame.cpp:139 / :1832 / :1862.
  PremixedFlameSettings omitted;
  omitted.cells = 500;  // set something else, deliberately NOT tolerance
  std::printf("after a caller sets other fields      %.6e   <- the default IS live\n",
              omitted.tolerance);

  // What every caller in this repository actually does.
  PremixedFlameSettings assigned;
  assigned.tolerance = 1.0e-3;
  std::printf("after a caller assigns 1.0e-3         %.6e   <- the default is dead-written\n",
              assigned.tolerance);

  std::printf("\n  tolerance -> terminal published bracket (the precision the solver claims)\n");
  for (const double tol : {1.0e-4, 1.0e-3}) {
    double q = 0.0;
    const int k = terminalBracket(tol, &q);
    std::printf("    %.1e  ->  k = %2d bisections,  q = %.6e  (%.4f %%)\n", tol, k, q, q * 100.0);
  }

  // The measured temporal error at the shipped cap, from the A4 dry-run. Printed so the coherence
  // arithmetic in the gate is reproducible from this probe rather than taken on trust.
  const double E = 0.152210e-2;
  std::printf("\n  measured temporal error at cap 5 (A4 dry-run)   %.6e  (%.6f %%)\n", E, E * 100.0);
  for (const double tol : {1.0e-4, 1.0e-3}) {
    double q = 0.0;
    terminalBracket(tol, &q);
    std::printf("    E / q at tolerance %.1e  =  %7.3f\n", tol, E / q);
  }
  return 0;
}
