# TD-032 ACCEPTANCE GATE — DESIGNED, **NOT YET FROZEN**

```text
written     2026-10-06, BEFORE any measurement of this item, so no threshold is fitted
status      DESIGNED. Freezing happens only after every criterion below has been dry-run
            against the UNCHANGED tree and non-vacuity shown in both directions (CLAUDE.md 5.2).
authorized  TD-032 in full: freeze gate, dry-run, fix, negative control, verify, regression.
scope       `src/species/PremixedFlame.cpp` time-level staging ONLY. No cleanup, no unrelated
            defect, no threshold anywhere else touched.
```

## 0. The defect being gated, and the fix being gated

`advance()` copies the transient term's old time level at lines 1189-1191, **before** the state is
updated at ~1210 (transport) and ~1235 (chemistry). From step 2 onward the old level trails the
state by one step, the transient path connects step `k-1` to step `k+1`, and the even and odd steps
become two interleaved sub-sequences. Audit: `audit.md`.

**The intended fix** is to capture `temperatureOld` and `compositionOld` at the TOP of `advance()`,
where the step begins, so the old level is by construction the state the step starts from.
`densityOld` stays at line 1191: it is a within-step quantity consumed by the `dt` controller at
1254-1260 and is already correct there. The fix adds no copy and no arithmetic — it moves two
existing statements — so its expected cost change is zero.

**Not in scope, recorded not done:** the fix makes `recentre()`'s shifts of `temperatureOld` /
`compositionOld` (lines 971-976) dead code. Removing them is cleanup and is forbidden here.

## 1. THE INSTRUMENT — why consecutive budgets, and why a RATIO

`finalMarchSteps` is a hard ceiling on the final march budget, so setting it to `N, N+1, ... N+4`
stops the march at exactly those step counts and the reported field is the state AT that step. The
solver is deterministic, so run `N+1` reproduces run `N`'s first `N` steps. Five runs give five
**consecutive states**. No production diagnostic is needed and none is added for the gate.

From those five reported temperature fields:

```text
across = max over the 4 ONE-step pairs  (k,k+1)  of  max_c |T_{k+1}(c) - T_k(c)|
within = max over the 3 TWO-step pairs  (k,k+2)  of  max_c |T_{k+2}(c) - T_k(c)|
R      = across / within
```

**Why `R <= 1` is a derivation and not a chosen constant.** For a sequence relaxing monotonically
toward a steady state the successive increments share a sign, so a two-step difference is the sum
of two same-signed one-step differences and is therefore at least as large as either of them:
`within >= across`, i.e. `R <= 1`. The exact value for a smooth sequence is `R ~ 0.5`.
`R > 1` requires the one-step difference to EXCEED the two-step difference, which is possible only
if the sequence is ordered by **parity** rather than by step gap. The threshold `1.0` is the sign of
that inequality. It bounds a dimensionless ratio of two temperature differences, so it needs no
scaling with mesh, `dt` or state.

**The vacuity branch, fixed now so it cannot be chosen later.** If `within <= 1e-9 K` the ratio is a
quotient of round-off and `R` carries no information. Round-off floor:
`eps_machine * T_scale * sqrt(nc) = 2.2e-16 * 1800 * 22 = 8.8e-12 K`, so `1e-9 K` sits two orders
above it. In that case **`R` is NOT evaluated**; the criterion instead requires
`across <= 1e-9 K` — a field stationary to round-off, which is a strictly stronger pass. Reporting
`PASS` from a vacuous ratio is forbidden.

## 2. THE CRITERIA

### C0 — INSTRUMENT SELF-TEST. Must PASS on the unchanged tree, before anything else.

The evaluator is applied to two synthetic 5-sample sequences built with no solver code:

```text
monotone relaxation  T_k = T_inf + (T_0 - T_inf) * exp(-k/tau)     must give R <= 1
parity alternation   T_k = A  for even k,  B for odd k, A != B     must give R >  1
```

This proves the criterion can both PASS and FAIL independently of CFDApp, which is what C1 and C2's
non-vacuity rests on while no corrected implementation exists yet (CLAUDE.md 5.2). **If C0 fails,
stop — the instrument is wrong and nothing below means anything.**

### C1 — the step-gap ordering is restored at the GATED FIXTURE

```text
fixture   500 cells, cap 5, relaxT 18, budgets 30945 30946 30947 30948 30949
          (the configuration every prior premixed-flame verdict was taken at)
criterion R <= 1.0, or the section-1 vacuity branch
```

Baseline prediction, registered before the dry-run, from `TD031/logs/05` on the five sampled
cells: `across ~ 1.4836e3 K`, `within ~ 2.47e-3 K`, `R ~ 6.0e5`.

**MEASURED (logs/01): `across 1.484054e3 K` at cell 37, `within 2.645849e-01 K` at cell 17,
`R = 5.608991e3` -> FAIL.** The criterion's verdict is as predicted and the fail-direction
non-vacuity is established, but **my predicted `R` was 107x too large and the prediction is
recorded wrong rather than quietly updated.** `across` was right to four figures; the whole error is
in `within`, which I predicted from the five cells `logs/05` happened to print and which the
criterion actually takes as a max over all 500. The real maximum sits at **cell 17**, far upstream of
the sampled window, and is 107x the sampled value. The consequence is not cosmetic: C3's original
margin was derived from that same sampled figure and is corrected below.

### C2 — the same at a SECOND configuration

```text
fixture   250 cells, cap 5, relaxT 18, budgets 7256 7257 7258 7259 7260
criterion R <= 1.0, or the section-1 vacuity branch
```

The five budgets are FIXED numbers here, not "whatever the fixture derives", so that the post-fix
run marches exactly as far as the baseline did. They come from the fixture-determination solve in
`logs/01` (derived budget 7260 at 250 cells, with the ceiling raised out of the way), which is
fixture setup and not a measurement of any criterion.

**MEASURED (logs/01): `across 1.484262e3 K` at cell 18, `within 2.858500e0 K` at cell 8,
`R = 5.192449e2` -> FAIL.** The defect is present at both fixtures, so the baseline fails here too.

The defect must be fixed everywhere, not at the fixture the evidence happens to use. `TD031/logs/01`
measured the alternation at 250 cells too, so the baseline must FAIL here as well.

### C3 — the per-step change stops being SATURATED

This is the quantity `finalMarchTolerance` is compared against, so C3 is what decides whether
*march until the state stops changing* becomes sound at all — the blocker under TD-028 and TD-031.

> **THE FIRST DRAFT OF THIS CRITERION WAS MIS-DERIVED, and the dry-run is why it is being replaced
> before the freeze and not after it.** The draft read:
>
> ```text
> criterion  result.temperatureResidual <= 1.0e-3 at the gated fixture
> ```
>
> justified as sitting "600x above the value a correct scheme is measured to produce", where that
> value came from the within-branch drift read off five sampled cells (`2.47e-3 K / 1500 K`).
> `logs/01` measured the full-field within-branch two-step difference as **0.2646 K**, 107x larger,
> so one step of one sub-sequence is about `1.76e-4` normalised and the real margin was about
> **5x**, not 600x. A 5x margin cannot tell a correct fix from a marginal one, so a correct fix
> could have FAILED C3 by accident. Per CLAUDE.md 5.2 the dry-run exists to catch a mis-derived
> criterion before freezing; the original is preserved above and the replacement is below. The
> replacement is **not** a loosening to obtain a pass — no fix exists yet to pass anything, and
> C3(b) is strictly harder to satisfy than the bound it replaces.

```text
C3(a)  result.temperatureResidual <= 1.0e-2 at the gated fixture
C3(b)  temperatureResidual(relaxT 26) <= 0.5 * temperatureResidual(relaxT 18), 500 cells, cap 5
```

**C3(a), the level.** The saturated value is the branch gap normalised by `(T_ad - T_u)`:
`1.484054e3 / 1500 = 0.9894`. `1.0e-2` is a factor **99 below** it. A residual alternation of even
1 % of the branch gap would therefore fail, and an alternation of any amplitude at all is caught
independently by C1, so C3(a) is a backstop on the LEVEL and is deliberately not asked to carry the
criterion.

**C3(b), the direction, and this is the part that discriminates.** A saturated quantity does not
move: TD-031 measured `temperatureResidual` at `0.989 +- 0.0002` across six configurations spanning
three meshes and two caps — a relative spread of `2e-4`. A quantity that is genuinely measuring a
converging march's per-step change must FALL as the march gets longer. The factor 2 is **1000x
outside that saturated spread**, so it cannot be met by noise, and because it asks for a DIRECTION
rather than a level it is immune to the sampling error that broke the first draft.

C3(b)'s fail-direction non-vacuity is measured separately on the unchanged tree in
`logs/02` — the lagged tree must show NO fall. A criterion the defective tree already satisfies
discriminates nothing and must not be frozen.

### C4 — the mixture constraint does not regress

```text
criterion  result.maxCompositionSumDeviation <= 2.0e-06 at the gated fixture
```

The header's exactness argument for the non-conservative time form sums the species equations using
`compositionOld`; the fix changes which field that is, so this is the quantity the fix puts at risk.
Direction only — it may improve, it may not worsen.

**The baseline is BIMODAL, which decides the reference.** `logs/01` measured, across the five
budgets, `8.692478e-07 / 1.627901e-06 / 8.690573e-07 / 1.627046e-06 / 8.688710e-07` — the two
branches of the alternation. The bound is taken from the **worst** of them, `1.627901e-06` rounded
up to one significant figure, and is labelled **EMPIRICAL** per CLAUDE.md 5.1. Taking the better
branch (`8.69e-07`) would be stricter, and is rejected deliberately: it would compare a post-fix
single-branch value against the cherry-picked better half of a bimodal baseline, which is not a
like-for-like comparison.

**Pre-registered reporting rule:** if the post-fix value lands between `8.69e-07` and `2.0e-06` it
will be reported as such — passing C4 while being worse than one baseline branch — and not as a bare
PASS.

### C5 — the converged EIGENVALUE does not move beyond the `dt`-truncation budget

```text
criterion  |S_L(fixed) - S_L(baseline)| / S_L(baseline) <= 3.99 %
baseline   S_L = 4.671937 cm/s at the gated fixture (logs/01), identical to six figures at all
           five budgets and therefore NOT affected by the alternation
```

The baseline's independence of the budget is itself worth recording: `S_L` is identical across all
five consecutive budgets and across both branches, which is the measured form of the claim that the
eigenvalue comes from the trials' drift SIGN. It also lands within 0.08 % of TD-027's Richardson
limit of 4.67589 cm/s, consistent with cap 5 now being the production default.

**Derivation.** At steady state the transient term vanishes identically whatever its form, so the
fixed point is a property of the spatial operator and the fix must not move it. `3.99 %` is the
already-measured `dt`-truncation error of this fixture against its Richardson limit
(4.86230 against 4.67589 cm/s, TD-027 C2): the fix may not move `S_L` by more than the entire
discretisation error it is not supposed to touch.

**Both directions pre-registered.** I expect the move to be far below 3.99 %, plausibly ~0, because
the bisection reads the SIGN of the drift. If it exceeds 3.99 % the fix changed the spatial problem
and **the item stops** — that is not a threshold to revisit.

### C6 — regression outside the flame is unchanged

```text
CFDIoTests        331
CFDAppLayerTests   32
CFDSpeciesTests   343   under --gtest_filter=-G8PremixedFlameTest.*
```

Exact counts, **re-measured on the unchanged tree during the dry-run** so the expectation is a
measurement on this tree and not a citation of an earlier aggregate.

### C7 — cost

```text
criterion  median of the 4 post-warm-up gated-fixture solves <= 695.1 s  (1.25 x 556.1 s)
           AND no test breaches the repository cap of ctest --timeout 7200
baseline   547 / 556 / 556 / 562 s post-warm-up, MEDIAN 556.1 s  (logs/01)
```

The fix moves two existing statements and adds no copy, so the expected change is zero. The factor
`1.25` is **EMPIRICAL**, derived from the observed run-to-run spread, and it is unchanged from the
draft: `logs/01`'s four post-warm-up solves span 547-562 s, a 2.7 % spread about the median, tighter
than the 5.5 % seen in `TD031/logs/05`, so 1.25 leaves ample margin without admitting a real
slowdown.

**The warm-up rationale did NOT reproduce, and the rule is kept anyway.** In `TD031/logs/05` the
first solve was the slowest (829 s against a 600 s median); in `logs/01` it was the FASTEST (542 s).
The pre-registered exclusion of the first solve is retained unchanged, because revising it on seeing
the data would be fitting. It is immaterial here: the median of all five solves is also 556 s.

### C8 — NEGATIVE CONTROL: the lagged library must FAIL

```text
criterion  the UNCHANGED tree fails C1 and C2, measured with the same probe source rebuilt
           against it, with both libraries' sha256 recorded
```

**MEASURED (logs/01): the unchanged tree FAILS C1 (`R = 5.609e3`) and C2 (`R = 5.192e2`), with
`libcfdcore.a` and both probe binaries' sha256 recorded in that log.**

This is the two-library attribution CLAUDE.md 6 requires, and the dry-run **is** the negative
control: the unchanged tree carries the lag. Both library hashes and both probe-binary hashes are
recorded so the fixed-library verification is demonstrably a different library.

## 3. STOP RULES

* **C0 fails → stop.** The instrument is wrong.
* Any of C1…C8 fails → **stop that criterion's item**, preserve the failure, do not weaken the
  threshold, report. In particular C5 exceeding 3.99 % is a finding about the fix, not about 3.99 %.
* A `PASS` reported from the vacuity branch without the `across <= 1e-9 K` check is a gate violation.
* No commit, no push. Neither is authorized.

## 4. WHAT THIS GATE DOES NOT CLAIM

It does not re-validate any premixed-flame number. **Every field-derived quantity in TD-004, TD-005,
TD-028, TD-029, TD-030 and TD-031 was measured on the alternating field and must be re-measured
after this fix**, under those items' own gates and their own authorizations. TD-027's C4, which its
closure rested on, is to be re-run on the fixed march with a ladder that CROSSES parity rather than
preserving it. None of that is in TD-032's scope.
