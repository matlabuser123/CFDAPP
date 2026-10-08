# TD-035 — the sanitizer policy, with the cost measured rather than argued

```text
opened      2026-10-07, carved out of TD-032
status      AUDIT + RECOMMENDATION. No production change, no test change, no preset change.
            Implementing the recommendation is NOT authorized and was not done.
severity    P1. It blocks TD-032's closure and TD-007, and it blocks §11.4 for every commit
            that carries production code.
evidence    logs/01_cost_ladder.log
```

---

## 1. The problem, restated from the measurement that created it

TD-032 measured ASan at **38x on the premixed-flame suite** (303 s -> 11612 s). G8's 4343 s becomes
~46 h, and at least three tests exceed `ctest --timeout 7200`, which is a **per-test** cap. So
`CLAUDE.md` §8's "sanitizers at CI settings" is not merely slow for this suite — it is impossible as
specified, and no amount of waiting fixes it.

**Raising `--timeout` is not available.** TD-009 already records the Debug+GUI timeout contract as
unstable; loosening the cap to accommodate one suite destabilises a contract that other work
depends on, and `CLAUDE.md` §7 forbids inflating a limit to obtain a pass.

## 2. What was measured

Release, relaxT 10, the A3-verified post-fix tree (`fa815c97…` / `852555cf…`):

```text
cells   wall s   marchSteps  recentrings   S_L cm/s   ASan est (s)   status
   60      7.0          269          112    4.47992            266   Converged
  100     10.5          643          135    4.82950            399   Converged
  150     19.2         1396          143    4.91857            728   Converged
  220     63.2         2975          155    4.94193           2401   Converged
  320    117.0         6260          184    4.95533           4447   Converged
  500    720.5        15283          223    4.95533          27379   Converged
```

**Every resolution converges and exercises the reacting path**, with 112-223 recentrings — so
`recentre`, `locateFront`, `shiftField` and the reactor all run. `S_L` climbs monotonically toward
the 500-cell `4.95533` and reaches it by 320 cells, so the coarse cases are solving the same
problem, not a degenerate one.

### 2.1 The exponent is NOT reported, and that is deliberate

```text
  60 ->  100   p = 0.79
 100 ->  150   p = 1.48
 150 ->  220   p = 3.12
 220 ->  320   p = 1.64
 320 ->  500   p = 4.07
```

**No single power law describes this.** The end-to-end fit is `p ~ 2.19`, but the interval values
swing from 0.79 to 4.07 — a factor of five — so quoting 2.19 would be a fabricated regularity. The
cause is structural rather than noise: the solve has **two phases with different scalings**. At 60
cells the bisection TRIALS dominate (269 march steps against 112 recentrings across the trials); at
500 the FINAL MARCH dominates (15283 steps). A cost model fitted across that transition is fitting
two regimes with one line.

The march-step count alone *is* clean — `steps ~ cells^1.70, 1.91, 1.96, 1.99, 2.01` — which is the
`dt ~ dx^2` diffusive limit, as expected. The wall time is not, because the trial phase is not
proportional to it.

**So the policy uses the MEASURED TABLE at measured points, not an extrapolated law.**

## 3. The recommendation

```text
RECOMMENDED   a dedicated sanitizer fixture at 150 cells, PLUS an explicit stated-scope record
              of what remains unsanitized.
```

### 3.1 Why 150 cells

```text
Release 19.2 s · ASan MEASURED 282.6 s · headroom against the 7200 s per-test cap: 25.5x

~~ASan est 728 s · headroom 9.9x~~ — SUPERSEDED by §7, which measured it instead.
```

The headroom is the point. The `38x` is an **extrapolation from one measured point** (§5), so the
fixture must survive that factor being wrong. At 150 cells the factor would have to be **~375x**,
nearly ten times the measured value, before the cap is threatened. At 320 cells the margin is only
`1.62x` and a modest underestimate breaks it; at 500 it is already 3.8x over.

### 3.2 Why a fixture rather than a smaller version of G8

A sanitizer looks for **memory errors, not physics**. A use-after-free or an overflow in
`shiftField` fires at 150 cells exactly as at 500 — it is the CODE PATH that must execute.

The G8 tests cannot simply be shrunk, because they assert physics that needs resolution: observed
order under mesh refinement, the eigenvalue plateau in anchor and domain, the adiabatic temperature.
Shrinking them breaks their assertions and would be **weakening tests to obtain a pass**, which §7
forbids outright.

A sanitizer fixture is therefore a **different test with the same code paths and cheap invariants**
(it ran, it converged, no leak, no overflow), not a shrunken G8. The two coexist.

### 3.3 What must be recorded, not silently accepted

Adding a cheap fixture does **not** make the heavy G8 tests pass under ASan — they remain in the
suite and would still time out. The policy therefore has a second half:

```text
the heavy G8 physics tests are EXCLUDED from the asan preset by label, and that exclusion is
recorded in TODO.md and ROADMAP.md as a standing scope limit, per CLAUDE.md §9's requirement that
a scope limit be written down rather than assumed.
```

**This is a real gap and must be stated as one:** the 500-1500-cell physics paths are exercised
under ASan by nothing. The fixture covers the same *functions*; it does not cover the same *problem
sizes*, and a bug that only manifests above some allocation size would be missed.

## 4. The options that were NOT recommended, and why

```text
raise --timeout            FORBIDDEN. TD-009 records the Debug+GUI timeout contract as already
                           unstable, and §7 forbids inflating a limit to obtain a pass.
stated-scope subset ALONE  Records the gap and closes nothing. It would leave the premixed-flame
                           code sanitized by NOTHING, when 728 s of coverage is available.
amend §8                   Not necessary and not proportionate. §8's text is right; what fails is
                           one suite's ability to satisfy it at full resolution. Amending the
                           project-wide regression definition to accommodate one suite is a much
                           larger change than labelling that suite.
delete or shrink the       FORBIDDEN by §7. Their assertions are the physics evidence for G8.
heavy tests
```

## 5. What this audit does NOT establish

```text
NOT measured     the ACTUAL ASan cost at 150 cells. Every "ASan est" column is Release wall time
                 times 38, and that 38 was measured ONCE, on the whole suite at full resolution
                 (303 s -> 11612 s). Applying it per-resolution assumes the overhead factor is
                 resolution-independent. That is plausible -- ASan's cost is per memory access --
                 but it is NOT verified, and the entire feasibility argument rests on it.
                 §6 is the measurement that would settle it.
NOT established  that a 150-cell fixture reaches the same CODE PATHS as a 500-cell one in every
                 branch. It converges and recentres 143 times, so the main paths run; branches
                 gated on large allocations or on step counts above ~1400 are not shown to run.
NOT established  that ASan is the only sanitizer at issue. CI runs five sanitizer configurations;
                 only ASan's cost was measured. UBSan and TSan costs are UNKNOWN for this suite.
NOT authorized   implementing any of this. It needs new test files, a CMake label and an asan
                 preset change, and none of that is in scope without a separate authorization.
```

## 6. The one measurement that would close §5's first gap

Build the `asan` preset and run the 150-cell case under it. That replaces the extrapolated `728 s`
with a measured number and either confirms the policy or kills it. It costs one ASan build plus
roughly a quarter-hour of run time, and it is the difference between a recommendation that is
measured and one that is argued.

**Until that is done, this audit recommends a policy whose central number is an extrapolation, and
says so.**

## 7. THE MEASUREMENT — §5's first gap is closed, and TD-035's headline number does NOT reproduce

`logs/02_asan_measured.log`, the `asan` preset at CI settings
(`ASAN_OPTIONS=detect_leaks=1:halt_on_error=0`, `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`):

```text
cells   Release   ASan MEASURED   real factor   the 38x estimate   status
  100     10.5 s        132.7 s        12.6x             399 s     Converged
  150     19.2 s        282.6 s        14.7x             728 s     Converged
```

### 7.1 The 38x does not reproduce, and that is a finding about TD-035 itself

**The measured factor is 12.6-14.7x, not 38x.** TD-035's founding number overstates the cost by
about 2.7x. The item's CONCLUSION survives — the heaviest tests still exceed the cap — but its
magnitude does not: G8's 4343 s becomes roughly 18 h at ~15x, not the ~46 h on record.

Why the suite-level 38x and the per-case ~14x differ is **NOT established**. Candidates: the suite
contains cases with a worse factor than the premixed-flame solve; the suite total included
per-process sanitizer startup and leak-check-at-exit across many binaries; or the two were measured
against different baselines. **This is not resolved here**, and TD-035's register entry should carry
the measured per-case factor alongside the suite figure rather than replacing one with the other.

### 7.2 No extrapolation is offered, deliberately

The factor rose from 12.6x to 14.7x across a 1.5x change in resolution. **Two points do not
establish a trend**, and fitting an exponent to them would repeat an error this campaign has already
made and recorded. So:

```text
MEASURED      100 cells 132.7 s, 150 cells 282.6 s. These two numbers only.
NOT MEASURED  220, 320 and 500 cells under ASan. If a larger fixture is wanted, MEASURE IT --
              do not scale 720.5 s by a factor read off two points.
```

What can be said without extrapolating: at **150 cells the fixture is measured at 282.6 s against a
7200 s cap**, which is 25.5x headroom, and no plausible error in a single measurement closes that.

### 7.3 The sanitizers found NOTHING, which is itself the first such evidence

```text
ERROR: AddressSanitizer    0
runtime error: (UBSan)     0
LeakSanitizer              0
```

Both resolutions converged with 135 and 143 recentrings, so `recentre`, `locateFront`, `shiftField`
and the reactor all executed under ASan+UBSan. **This is the first sanitizer coverage the
premixed-flame code has ever had** — TD-035 exists precisely because it had none — and it is clean.

A caution against over-reading it: a clean sanitizer run on a path that executes is evidence about
that path at that size, and nothing more. `CLAUDE.md`'s own fail-open lesson applies — a sanitizer
log with no findings looks identical whether the code is clean or the instrument never reached it.
Here the instrument demonstrably reached it: 1396 march steps and 143 recentrings at 150 cells.

### 7.4 What this does to the recommendation

**It strengthens it and does not change it.** 150 cells remains the recommendation, now on a
measured 25.5x headroom rather than an estimated 9.9x. 320 cells becomes more plausible than §3.1
suggested — but it is UNMEASURED under ASan, and the point of §7 is that unmeasured numbers are
what put TD-035's 38x on the record in the first place.
