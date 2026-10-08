# TD-037 ACCEPTANCE GATE — DESIGNED, **NOT YET FROZEN**

```text
written     2026-10-07, BEFORE any measurement for this item, so no threshold is fitted
status      DESIGNED. Freezing happens only after every criterion below has been dry-run against
            the UNCHANGED tree and non-vacuity shown in both directions (CLAUDE.md §5.2).
authorized  TD-037 in full.
scope       the drift-reading condition in `runTrial`'s EXTINCTION path ONLY. No change to
            `trialSaysTooLarge`, to the normal path's window-boundary logic, to any threshold
            elsewhere, or to any test.
```

## 0. The defect and the intended fix

A trial that extinguishes with no more than `driftWindow` positions of front-position history is
unsidable (`PremixedFlame.cpp:1384`), so `trialSaysTooLarge()` returns false and the search aborts.
Measured: the same trial at 40 steps is sided at `driftWindow` 32 and unsided at 64. Audit:
`audit.md`.

**The intended fix** is to read the extinction drift over whatever history exists:

```text
effective window = min( driftWindow, history.size() - 1 ),  floored at 4
```

**The floor is derived from this code's own standard**, not invented: line 1344 already applies
`std::max<Index>(4, settings.driftWindow)`, so a 4-position window is what the solver already
treats as the smallest meaningful one.

**Explicitly NOT in scope.** `trialSaysTooLarge()` fails closed and must keep failing closed — an
earlier version inferred the side and halved the mass flux thirteen times without bracketing
anything. The unsided case must never guess. This gate changes what is *measurable*, never what is
*assumed*.

## 1. The fixture

The test's own `makeSettings()`, which overrides neither the diffusion cap nor the march length:

```text
cells 500 · domainLength 0.010 m · anchorFraction 0.35 · tolerance 1e-3
maxOuterIterations 40 · driftSettleTolerance 0.05 · maximumStepsPerTrial 20000
driftWindow ∈ {16, 32, 64, 128}        <- the only variable
```

16 and 32 are below the measured 40-step trial; 64 and 128 are above it. The ladder therefore
straddles the boundary rather than sampling one side of it — the error that made TD-027's C4
unsound.

## 2. The criteria

### C0 — REPRODUCE. The unchanged tree must FAIL at `driftWindow` 64.

```text
criterion  on the unchanged tree, driftWindow 64 returns a status other than Converged, and its
           trial record contains at least one trial with `measured == false`
```

A gate whose defect does not reproduce at its own named fixture is measuring something else. This
criterion exists to be **failed** by the baseline; if it passes there, nothing below may be frozen.

### C1 — the search completes across the whole ladder.

```text
criterion  status == Converged at driftWindow 16, 32, 64 AND 128
baseline   16 and 32 expected Converged; 64 measured NoFlame; 128 unmeasured -> the dry-run
           establishes it rather than assuming it
```

### C2 — `S_L` MUST NOT MOVE. This is the criterion that can stop the item.

```text
criterion  |S_L(fixed) - S_L(baseline)| / S_L(baseline) <= 1.0e-3, at every window where the
           BASELINE converged -- which the dry-run establishes is windows 16 and 32
baseline   S_L = 4.671937 cm/s and final mdot = 0.0550371757 at BOTH windows, identical to every
           digit printed, with identical trial counts and identical C3 outcomes. The two windows
           run the same search, which is what makes them a usable reference.
```

**Derivation.** `S_L` is produced by bisection that terminates when `(high - low)/low <= tolerance`,
and `tolerance` is `1e-3` at this fixture. The search's own convergence tolerance therefore bounds
how far two valid searches for the same eigenvalue may land apart. The bound is the solver's
parameter, not a number chosen to fit an observation.

**Why this can stop the item.** The fix admits trials that were previously unsided, which changes
the bracketing path — and `S_L` is read from the drift's *sign*. A fix that moves the eigenvalue
beyond the search's own tolerance has traded a hard failure for a silent change of answer, which is
worse. If C2 fails, **stop**; do not widen it.

### C3 — every sided trial must be sided CORRECTLY.

```text
criterion  for every trial with `measured == true`: tooLarge == (massFlux > S_L_final * rho_u)
```

A drift measured over a short window on an extinguishing flame might report the extinction
transient rather than the propagation, and would then assign a **wrong** side — worse than
undecided, and the reason §2 of the audit forbids guessing. The final eigenvalue gives an
independent answer for which side each trial's mass flux was on.

**Dry-run both directions:** on the baseline this must PASS over the trials that *are* already
sided, which proves the check is correct and that existing sides are self-consistent. Its reach
extends to the newly-sided trials only after the fix, and that limitation is stated rather than
hidden.

### C4 — the normal path must be untouched.

```text
criterion  at driftWindow 32 the trial record is IDENTICAL to the baseline's -- same trial count,
           and same massFlux, steps, measured, settled, extinguished and tooLarge for each
baseline   15 trials, all measured, recorded verbatim in dry_run.md section 3. The shortest is
           trial 3 at 40 steps, and 40 > 32, so the clamp min(32, 39) returns 32 unchanged.
```

Every trial at window 32 has more than 32 positions of history (the shortest measured is 40), so
none is unsided there and the clamp `min(32, history-1)` returns 32 unchanged. The record must
therefore be bit-for-bit the same sequence. If it is not, the fix has reached the normal path.

### C5 — the test TD-032 broke must pass again.

```text
criterion  G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial PASSES, and the
           drift-window spread it prints stays below its own 0.02 bound
baseline   FAILS -- ASSERT_EQ(flame.status, Converged) at test_premixed_flame.cpp:725 returns
           NoFlame at driftWindow 64
```

The test's threshold is **not** touched. It is the test's own, it was frozen long before this item,
and this gate's job is to make the solver satisfy it rather than the reverse.

### C6 — regression outside the flame is unchanged.

```text
CFDIoTests 331 · CFDAppLayerTests 32 · CFDSpeciesTests 343 under
--gtest_filter=-G8PremixedFlameTest.*
```

Re-measured on the unchanged tree during the dry-run, so the expectation is a measurement on this
tree rather than a citation.

### C7 — cost. **RE-DERIVED after the dry-run; the draft was unsound.**

```text
criterion  no test breaches the repository cap of ctest --timeout 7200
reported   per-window solve times and the C5 test's wall time, WITHOUT a bound
```

> **The draft added "the gated-fixture solve at driftWindow 32 does not exceed 1.25x its baseline
> wall time", and the dry-run shows why that cannot stand.** The baseline at windows 64 and 128
> **aborts after 4 trials** instead of running 15, so the work it does is not the work a fixed
> solver does. The C5 test takes 770 s on the baseline precisely *because* window 64 quits in
> ~14 s; a correct fix makes window 64 complete, so the test MUST get slower, probably by the cost
> of a whole extra search. Bounding a repaired path against the cost of an aborting one measures
> nothing, and any factor chosen now would be fitted to make the expected outcome pass.
>
> So the cost criterion keeps only the constraint that is the **repository's own** — `ctest
> --timeout 7200` — and the wall times are reported as information rather than gated. The original
> clause is preserved above rather than deleted.

### C8 — NEGATIVE CONTROL: the unguarded library must FAIL.

```text
criterion  the UNCHANGED tree fails C0 (by reproducing), C1 and C5, measured with the same probe
           source rebuilt against it, with both library sha256 recorded
```

The dry-run **is** the negative control, as it was for TD-032: the unchanged tree carries the
defect. Both library hashes are recorded so the fixed-library verification is demonstrably a
different library.

## 3. Stop rules

* **C0 passing on the baseline → stop.** The defect does not reproduce and the fixture is wrong.
* **C2 failing → stop.** A moved eigenvalue is not a threshold to revisit.
* **C3 failing → stop.** A wrongly-sided trial is worse than an undecided one.
* **C4 failing → stop.** The fix has escaped its scope.
* Any other criterion failing → stop that criterion's item, preserve the failure, do not weaken
  the threshold, report.
* No commit, no push. Neither is authorized, and TD-034 blocks commits regardless.

## 4. What this gate does not claim

It does not re-validate any premixed-flame number, and it does not close the G8 validation. It
establishes that the search can decide a side for every trial across a `driftWindow` ladder that
straddles the measured trial lifetime, without moving the eigenvalue. TD-027, TD-028, TD-029,
TD-030 and TD-031 remain to be re-measured under their own gates, and TD-036's mislabelled status
is not repaired here — a search that no longer aborts will simply stop exercising that path, which
is not the same as fixing it.
