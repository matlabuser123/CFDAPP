# TD-004 AMENDMENT A7 — C6a-ord's resolution UNIT. Proposed. Floor, band, formula and triplet all unchanged.

```text
AUTHORIZED:  by the user, 2026-10-13, for the amendment derived in audit_C6a_ord.md §5.
STATUS:      PROPOSAL + INSTRUMENT READY. Nothing is frozen and nothing has been run for it: the
             machine is committed to TD-027's GATE_A4 fresh verification until roughly 02:00, and
             the hardware policy forbids a competing heavy run. §5.3's order is
             proposal -> dry-run -> non-vacuity -> FREEZE -> rerun fresh.
WHAT MOVES:  the UNIT C6a-ord counts resolution in. Nothing else.
```

## 1. THE SUBSTITUTION

```text
A6   "one quantum is (2^(1/1024) - 1) of S_L at the middle level"

A7   "one quantum is the TERMINAL BISECTION BRACKET at the tolerance the triplet was run at:
      2^(1/2^k) - 1 for the first k with that <= tolerance -- which IS (2^(1/1024) - 1) at
      tolerance 1e-3, and (2^(1/8192) - 1) at 1e-4"
```

`PremixedFlame.cpp:1832` brackets to `high/low = 2` and bisects on the **geometric** midpoint, so
the bracket after `k` steps is `2^(1/2^k) - 1` and the loop stops at the first `k` reaching the
tolerance. A6's constant is that bracket at `1e-3` **and at no other tolerance**. C6a-ord's
`(N-1, N+1)` bound treats one quantum as the measurement's own uncertainty, so counting in a unit
that is not the resolution actually achieved is incoherent — which is why `n2 = 2.00` was a property
of the probe's tolerance choice rather than of the solution family.

**UNCHANGED, and this is the whole point:** the 4-quantum floor, the `[1.5, 2.5]` band, §3(b)'s
`p_lo = log2((n1-1)/(n2+1))`, `p_hi = log2((n1+1)/(n2-1))`, the `{125, 250, 500}` triplet, the four
admissibility clauses, and C6a-bal's `h^2` bound. **Verified identical** between the two probes by
direct comparison: `kQuantumFloor = 4.0`, `kBandLo = 1.5`, `kBandHi = 2.5`,
`kBalanceAt500 = 0.02`, `kReferenceCells = 500`, `kResolutionGuard = 4.0`.

## 2. THE INSTRUMENT

`tools/c6a_a7_probe.cpp`, built from `tools/c6a_amended_probe.cpp`, which is GATE_A6's evidence and
is **not modified**. The diff is six changes and nothing else:

```text
the title line                           so its output cannot be mistaken for A6's
#include <cstdlib>                       for atof
int main(int argc, char** argv)          the tolerance comes from argv[1], default 1.0e-3
the quantum                              computed from `tol`, replacing the hardcoded constant
the quantum announcement                 the log records k and the tolerance that produced it
s.tolerance = tol                        was 1.0e-3
```

Compiles clean under `-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic`. One binary serves both
non-vacuity cases, so there is no second instrument to keep in step.

## 3. PRE-REGISTERED PREDICTIONS

From `audit_C6a_ord.md` §4. A deviation is a result, not something to absorb.

```text
run at 1e-3   k = 10   q = 6.771307e-04   n1 = 12.94   n2 =  2.00   -> UNRESOLVED   (reproduces A6)
run at 1e-4   k = 13   q = 8.461627e-05   n1 = 103.6   n2 = 16.0    -> RESOLVED
                                          p in [2.593, 2.801] against [1.5, 2.5]  -> FAIL
```

The quantum ratio is `8.002370`, **not exactly 8**, which is why `n1` is 103.6 and not 103.5.

**C6a-ord is predicted to become a DEFINITE FAIL.** A7 is not proposed to obtain a pass, and
`verification_A6.md` §6 predicted this outcome before this amendment existed.

## 4. NON-VACUITY

```text
MUST report UNRESOLVED at 1e-3   or the amendment has destroyed the resolution guard, and a
                                 criterion that resolves at every tolerance is not measuring
                                 resolution at all
MUST report RESOLVED at 1e-4     or the substitution achieves nothing
MUST reproduce A6's numbers at 1e-3   S_L 4.90527 / 4.94863 / 4.95533, n1 = 12.94, n2 = 2.00,
                                 C6a-bal PASS at all three levels. A deviation means the fixture
                                 is not reproducible, and that is the finding instead.
```

The first two are the real control: the same binary must give opposite verdicts on the two
tolerances. The third is a reproduction check against frozen evidence.

## 5. COST — corrected upward from the audit

`audit_C6a_ord.md` §5 costed "30-45 minutes". **That covered one run, not the §5.3 cycle.** The
full cycle needs four triplet runs, because step 6 forbids reusing a dry-run result:

```text
dry-run    1e-3    ~818 s      (A6 measured 17 + 88 + 713 for 125/250/500)
dry-run    1e-4    ~1600-2500 s  (A4 measured this k = 10 -> 13 change at about 2x)
fresh      1e-3    ~818 s
fresh      1e-4    ~1600-2500 s
TOTAL              ~4800-6600 s  =  1.3 to 1.8 h
```

Still cheap next to TD-027's 9.5 h ladder, but it is not 30-45 minutes and the earlier figure is
corrected here rather than left standing.

## 6. WHAT A7 WILL NOT SETTLE

A resolved `p ≈ 2.7` still admits two readings, and no tolerance separates them:

```text
(i)  the family's observed order really is ~2.7, so the frozen band is violated;
(ii) {125, 250, 500} is not yet asymptotic, so the band is being applied outside its domain.
```

`{63, 125, 250}` gives `p = 3.176` post-fix and `0.523` pre-fix — failing on **both** libraries — so
63 cells is outside the asymptotic range, and whether 125 is inside is **not established**.
Separating (i) from (ii) needs a finer **mesh** triplet; `{250, 500, 1000}` is unaffordable, the
1000-cell level having been killed at exit 124 against the real `ctest --timeout 7200` cap.

**So A7 buys a definite verdict and leaves its interpretation open.** That is still worth having —
`verification_A6.md` §6 lists "that `p ~ 2.69` is the family's true order" as NOT established, and
A7 settles exactly that — but it does not unblock TD-004, which also still carries TD-040's
`S_L +6.07 %` stop.

## 7. SEQUENCE FROM HERE

```text
1  WAIT for GATE_A4's run. The probe's own guard would refuse to start beside it.
2  DRY-RUN at 1e-3 and at 1e-4 on the unchanged tree; check §3 and §4.
3  FREEZE GATE_A7.md and record every hash in logs/A7_00_freeze.log.
4  RERUN FRESH at both tolerances, reusing no dry-run result (§5.3 step 6).
```

Step 3 cannot precede step 2. No production file is changed at any step.
