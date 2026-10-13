# TD-003 C3 — AMENDMENT A1 is WITHDRAWN, the feedback hypothesis is REFUTED, and a CLAIM I ALREADY COMMITTED is wrong.

```text
STATUS:      **THREE WITHDRAWALS, one of them of committed evidence.**
             1. AMENDMENT A1 (the derived bound) FAILS its own pre-registered dry-run D1 by a
                factor of 6.2e+06, in BOTH configurations. It is withdrawn, and `T` is NOT
                adjusted -- the amendment committed to that before the run.
             2. The FEEDBACK hypothesis is REFUTED by a causal control: disabling the closure
                correction leaves the residual essentially unchanged.
             3. **`verification_closure_test.md`'s central claim -- "round-off in the RHS
                amplified by a near-singular operator" -- is WRONG on BOTH halves.** The
                operator is well-conditioned (condition number 33) and the residual is 5.6e+08
                times larger than round-off in these terms.
             **C3's cause is therefore UNKNOWN again**, and the 1e-8 threshold stands untouched
             with the test still failing.
SCOPE:       the dry-run and its control. No production change, no test change, no threshold
             change. The amendment file is preserved, not deleted, and marked withdrawn.
AUTHORIZED:  Option A was authorized; §5.3 step 6 ("rerun fresh") was executed and the
             amendment failed its own dry-run. That is the authorized process reaching a
             negative result, not an unauthorized change.
EVIDENCE:    logs/05_amendment_dryrun.log · logs/06_feedback_test.log · logs/07_nv1_control.log
COMMIT:      carried as evidence.
```

---

## 1. D1 FAILED — the amendment is withdrawn, not re-derived

```text
                                     PRODUCTION (A=5, T=24)   NV1 CONTROL (A=4, T=19)
DERIVED BOUND  T*eps*||A^-1||*(..)        3.719907e-13             2.944929e-13
MEASURED       max_c |sum_i y_EXACT-1|    2.301527e-06             1.748390e-06
D1 HOLDS                                  *** NO ***               *** NO ***
shortfall                                  6.2e+06x                 5.9e+06x
```

`AMENDMENT_A1_sumY_bound.md` §4 pre-registered the response: *"If D1 fails, the amendment is
wrong and the failure is preserved; `T` is NOT adjusted to make it pass."* `T` is unchanged at 24.

**Two of the three dry-run criteria did pass**, which matters for what the failure means:

```text
D2  the original 1e-8 still FAILS         PASS -- so a verdict change would have been
                                          attributable to the amendment, not to tree drift
NV2 the bound with ||A^-1|| -> 1 fails    PASS -- 3.099917e-18, so the amplification factor
                                          was load-bearing rather than decoration
```

### 1.1 Why the derivation was wrong, located exactly

The bound needed `2||b||_inf + ||A||_inf >= 3599`. Measured at the worst state:

```text
||A||_inf     2.733828e-04          I had assumed ~1e+00 .. 1e+04
||b||_inf     1.541582e-04
```

The assembly is in **FLUX units**: coefficients scale with the face area, `~dx = 2e-5`, so
`gamma*A/dx ~ 4.2e-05`. My earlier hand-derivation used a unit face area and produced `D = 2.08`,
**1e4 too large**, which is what made `3599` look reachable. The derivation's FORM may still be
right; its scale factor was wrong, and no honest adjustment of `T` can bridge 6.2e+06.

## 2. THE FEEDBACK HYPOTHESIS — REFUTED by a causal control

The hypothesis: A1's cancellation `sum_i K(Y_i) = K(sum_i Y_i) = K(1)` is exact only if
`sum_i Y_i` is exactly 1, so on an iterate it leaves `K(d)` and the residual becomes proportional
to the deviation already present -- a near-unity-gain loop.

**NV1 removes the hypothesized cause**: `closureCorrection = false`, the flag
`SpeciesTransport.hpp:168-171` provides for exactly this control, and the one A1's own NV3 used.

```text
                                  PRODUCTION      NV1 (correction OFF)     change
max_c |sum_i b_i - (A*1)_c|      3.162982e-11    2.734989e-11            -14 %
max_c |sum_i y_EXACT - 1|        2.301527e-06    1.748390e-06            -24 %
S_L                              4.955332        4.955332                BIT-IDENTICAL
```

**A feedback through `K` would collapse when `K` is removed. It did not.** REFUTED.

### 2.1 And the control is WEAK here, which I should have predicted

```text
sum_j J^0_{j,f} = -gamma * d(sum Y)/dx * A
```

With `Le = 1` every species shares one `gamma`, and `sum Y` is uniform to ~1e-6, so
**`rawSum ~ 0`** and the correction multiplies a near-zero. That is also why `S_L` is
bit-identical with it disabled. A1's NV3 discriminated in P18.2's case because `rawSum` mattered
there; in this flame it removes almost nothing. The control is valid as a refutation (removing a
term that does nothing cannot be feeding the residual) but it is NOT strong evidence about the
closure's general behaviour, and a cheaper prediction of `rawSum ~ 0` would have saved the run.

## 3. THE CORRELATION TEST WAS CIRCULAR — my instrument, not a finding

`logs/06` reported `corr(r, d) = 0.894703`, `98.8 %` sign agreement, and `r/d -> 8.31e-06`
against `1/||A^-1||_inf = 8.3333e-06`. **None of it discriminates anything.**

```text
d is DEFINED as A^-1 r (measured to seven digits in verification_instrumented_residuals.md).
A^-1 is a POSITIVE operator here -- a diagonally dominant tridiagonal with negative
off-diagonals inverts to all-positive entries -- so d inherits r's sign structure AUTOMATICALLY,
and r/d ~ 1/||A^-1|| follows for ANY r aligned with the dominant mode, whatever produced it.
```

So the test correlated `r` against `A⁻¹r` and recovered the identity it was built on. **This is
the "instrument downstream of the unknown" failure**, committed in a test designed specifically
to avoid a circular single-number version of the same match.

What survives, narrowly: the deviation in the burnt region (cells >= 210) lies in `A^-1`'s
dominant mode to **0.3 %**, while cells 0-168 give ratios `1.7e-05 .. 1.8e-04` where that mode has
not established.

## 4. A COMMITTED CLAIM IS WRONG — `verification_closure_test.md`, corrected forward

That file (committed in `7e4a3a1`) states the deviation is *"round-off in the RHS amplified by a
near-singular operator"*. **Both halves fail.**

```text
"near-singular"   ||A||_inf * ||A^-1||_inf = 2.733828e-04 * 1.200002e+05 = 33
                  The operator is WELL-CONDITIONED. ||A^-1||_inf is large only because the
                  coefficients are SMALL (flux units, ~1e-4), not because A is near-singular.
"round-off"       ||r||_inf ~ 1.9179e-11   against   eps * ||b||_inf = 3.4230e-20
                  a factor of 5.60e+08. It is not rounding of these terms by eight orders.
```

**The error was a norm comparison across two different states.** The original "3.162982e-11 is
3.5x the round-off floor" compared a residual with `eps * ||b||_2(max over the whole march)`,
while the residual belongs to one step whose `||b||_inf` is `1.5e-04`. Section 7 of that file
listed several things as not established; this was not among them, because I did not see it.

Per §4 and §10 the file is committed and is corrected FORWARD here rather than rewritten; a
forward note is added to it pointing at this document.

## 5. WHAT IS AND IS NOT ESTABLISHED NOW

```text
STILL ESTABLISHED
  d = A^-1 r, to seven digits
  the per-species matrices are BIT-IDENTICAL (max |A_i - A_0| = 0.0), so the telescoping is exact
  the inlet, seed, chemistry sources and recentring fill each sum to 1 EXACTLY
  the linear solve contributes ~1 % of the deviation; an exact solve leaves 99.97 %
  r ~ 3e-11 is NOT the closure correction (section 2) and NOT round-off in these terms (section 4)
  A is well-conditioned: condition number 33

NO LONGER ESTABLISHED
  that the deviation is round-off amplified by ill-conditioning          -- section 4
  that the RHS "closes only to round-off"                                -- section 4
  the derived bound of AMENDMENT A1                                      -- section 1
  the feedback mechanism                                                 -- section 2

NEVER ESTABLISHED
  the cause of r ~ 3e-11. SIX hypotheses have now failed in this item, and this round took a
  COMMITTED finding with it. The next instrument must ATTRIBUTE rather than hypothesise:
  assemble with each contributor enabled in isolation and difference the row sums, so r is
  charged to a specific assembler instead of to a story.
```
