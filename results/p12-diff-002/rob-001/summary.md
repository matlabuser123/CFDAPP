# P12-DIFF-002-ROB-001 — Divergence detector floor consistency — report

**Gate:** `results/p12-diff-002/rob-001/acceptance_gate.md`, sha256
`1e2c98684b62b14418dd745ae7c5b8397b0547fb073212196c6fdc64753d3a2d`, frozen **before any production
modification** (`logs/03_gate_freeze.log`).

```text
R1  reproducer fails first            PASS   detectors OFF/ON differ; rule 2 unfloored fires@112
R2  minimal production change         PASS   2 functional lines, completeness proven by hash
R3  baseline invariance               PASS   field difference exactly 0.0000e+00; test file unchanged
R4  genuine divergence still fires    PASS   Diverging, finite, growth 1.66e+14, detail intact
R4b pre-registered known failure      PASS   only DivergenceStatus's iterations==20 assertion fails
R5  direct rule-2 non-vacuity         PASS   (a) no fire, (b) no fire, (c) FIRES at 15
R6  rest of SolverRobustness          PASS   CFDSolverTests 90/90; 9 of 10 SIMPLERobustnessTest
R7  mesh/tolerance/window sensitivity PASS   0 healthy runs altered, 0 runaways missed
R8  fresh focused regression          PASS   680 run, 676 pass, 4 fail (all pre-registered)
R9  scope                             PASS   everything else byte-identical
```

**Verdict: the false positive is fixed, genuine divergence detection is intact, and nothing else
changed.** A6's `BLOCKED — PRODUCTION DEFECT FOUND` stop is preserved verbatim as the historical
record; it was correct when made.

---

## 1. Root cause

`src/solver/SolverRobustness.cpp::OuterIterationMonitor::diverging` applied two growth rules to the
same residual tracker with **inconsistent floors**:

| | base of the comparison | floored? |
| --- | --- | --- |
| Rule 1, persistent excursion (389–400) | `max(tracker.floor(), bestBeforeWindow)` | **yes** |
| Rule 2, sustained runaway (401–415) | `oldest = valueAgo(window − 1)` | **no** |

`tracker.floor()` *is* that residual's convergence tolerance — `threshold()` returns it as the
absolute convergence threshold — so rule 1 can never fire on an already-converged residual while
rule 2 could, and did.

## 2. Pre-fix reproducer (R1)

`tools/rob001_reproducer.cpp`, `logs/01_prefix_reproducer.log`. `cavity(4, 0.01)`, α = 0.7/0.3,
tol 1e-6, maxIterations 500 — `DefaultRobustnessPreservesBaseline`'s own case:

```text
detectors OFF: Converged   406 it   final u 3.0310e-08  cont 3.3087e-13
detectors ON : Diverging   112 it   final u 1.6967e-05  cont 3.1002e-13
detail: divergence: the continuity residual increased at each of the last 9 iterations,
        1.958e-14 -> 3.1e-13 (>= 10x)
field difference OFF vs ON: u 7.9778e-04  v 5.6119e-04  p 7.5178e-04     (test requires exactly 0)
```

Independent replay of both rules on **every** tracked residual of the healthy reference run:

```text
u, v, pressure   rule1 no fire | rule2 UNFLOORED no fire | rule2 FLOORED no fire
continuity       rule1 no fire | rule2 UNFLOORED fires@112 | rule2 FLOORED no fire
```

The recorded quantities at the trip:

| quantity | value |
| --- | --- |
| oldest | 1.9578e-14 |
| current (newest) | 3.1002e-13 |
| tracker floor = convergence tolerance | 1.0e-06 |
| growth ratio | 15.835 |
| floor / oldest, floor / newest | 5.108e+07, 3.226e+06 |
| u residual at that iteration | 1.696673e-05 — **not** converged, so the solve was legitimately still running |
| continuity over the whole window [102…111] | 1.958e-14 … 3.100e-13, **every value already converged** |

All ten window values sit below the tolerance; with the floor applied the comparison becomes
`3.1002e-13 >= 10 × 1.0e-06 = 1.0e-05` → suppressed. Stagnation was measured too and would **not**
fire (worst windowed improvement 6.478e-01 against the 1e-2 threshold), so nothing else was waiting
to abort the run.

## 3. The production change (R2)

`logs/05_R2_production_diff.log`. Two functional lines in `diverging()`:

```diff
   const Real oldest = tracker.valueAgo(window - 1);
-  if (increasing && tracker.valueAgo(0) >= growth * oldest) {
+  const Real meaningfulOldest = std::max(tracker.floor(), oldest);
+  if (increasing && tracker.valueAgo(0) >= growth * meaningfulOldest) {
```

plus the comment recording why. `oldest` stays raw for the diagnostic message, which therefore still
reports what was actually observed.

**Completeness proven, not asserted:** reverse-applying this edit reproduces the frozen pre-fix file
byte-for-byte —

```text
reconstructed  d210001a944ee498d7ae2012c8683433fa8b922415659ac8d511826e4e0a151c
frozen pre-fix d210001a944ee498d7ae2012c8683433fa8b922415659ac8d511826e4e0a151c   -> MATCH
```

so there is no other production edit hiding in the file. `SolverRobustness.hpp` and all 15 other
production numerical files are byte-identical. Growth factors, windows, start iterations, convergence
tolerances, the stagnation rule, the fallback rule, the adaptive controller and the non-finite path
are untouched; nothing is special-cased and rule 2 is not disabled.

## 4. Baseline invariance (R3)

`logs/06_postfix_verification.log`, `logs/07_postfix_reproducer.log`.
`SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline` **passes**, with its file byte-identical to
`97fc7b61…` — it was not modified. Directly measured:

```text
detectors OFF: Converged   406 it   u 3.0310e-08  v 3.1141e-08  p 9.9192e-07  cont 3.3087e-13
detectors ON : Converged   406 it   u 3.0310e-08  v 3.1141e-08  p 9.9192e-07  cont 3.3087e-13
field difference: u 0.0000e+00   v 0.0000e+00   p 0.0000e+00
same status: yes   same iterations: yes
```

The robustness layer is observationally inert again when no intervention is required.

## 5. Genuine divergence still fires (R4/R4b)

The genuine runaway of `DivergenceStatus` is still caught:

```text
status 8 (Diverging), 21 iterations, final u 1.26e+13 v 1.71e+12 p 1.28e+06, fields finite
growth final u / first u = 1.664e+14   (the test's own bound is 1e3)
detail: divergence: every one of the last 10 u residuals is >= 10 x its best value since
        iteration 10 (1.093e+04); window minimum 2.864e+05, latest 1.255e+13
```

Its residuals are 1.09e+04 … 1.26e+13, far above any floor, so `meaningfulOldest == oldest` there and
rule 2 behaves exactly as before.

**R4b, pre-registered:** the test still fails on its single `EXPECT_EQ(result.iterations, 20u)`
assertion (21 vs 20). A6 classified that **MIGRATE_POLICY** — `startIteration + window = 20` is the
earliest the detector *can* fire, a lower bound rather than a contract value — and ROB-001 is
forbidden to migrate it. Every other assertion in the test passes. This is not a ROB-001 regression,
and `DivergenceStatus` was **not** amended.

## 6. Direct rule-2 non-vacuity (R5)

Three sequences added to `tests/unit/solver/test_solver_robustness.cpp`, driving the monitor
directly. They vary the **continuity** channel while holding u/v/pressure above tolerance, because a
sample whose every residual is sub-tolerance is `Converged` before the divergence rules run — that is
how production reaches the rule, and getting this wrong would have made the test vacuous.

| sequence | pre-fix | post-fix | required |
| --- | --- | --- | --- |
| (a) healthy decreasing, above floor | Continue | Continue | no fire |
| (b) sub-floor round-off growth (×2 per step → 16× across the 5-window, max 5.24e-09) | **Diverging @15** | Continue | **no fire** |
| (c) above-floor runaway (1e-3 → 1e+1, same monotone shape) | Diverging @15 | **Diverging @15** | **must fire** |

(c) is the power case, on the same channel that was false-positiving, and it fires *through rule 2*
(rule 1 cannot: its `bestBeforeWindow` is empty at that iteration). (b) failed before the fix and
passes after — the reproducer-first discipline.

**A vacuity trap caught by the gate's own dry-run column:** (b)'s first draft grew 100× over 19
iterations, i.e. only 2.68× across the 5-wide window, so it never reached the 10× factor and passed
pre-fix. The gate had pre-registered that (b) must fire pre-fix, which exposed it; the sequence was
corrected to double each step, not the criterion.

The pre-existing `ResidualTrend.SustainedRunawayFromAPlateau` is the codebase's own above-floor rule-2
control (oldest 1e-3 against a 1e-6 floor) and still passes, as do `Diverging`,
`SingleSpikeDoesNotTriggerDivergence` and `NonFiniteResidualIsDivergingWhenEnabled`.

## 7. The rest of SolverRobustness (R6)

`CFDSolverTests`: **90/90 pass** (87 pre-fix plus the 3 new sequences) — real divergence, stagnation,
slow-convergence-is-not-stagnation, single spike, non-finite detection, adaptive-relaxation recovery,
settings validation and bookkeeping cost all unchanged. All 11 `ResidualTrend` tests pass.

`SIMPLERobustnessTest`: **9 of 10 pass** (was 8 of 10), the only failure being R4b's assertion.
`StagnationStatus`, `NormalConvergenceIsNotStagnationOrDivergence`, `AdaptiveRelaxationRecovery`,
`LinearSolverFallbackRecovery`, `NormalizedResidualConvergence`, `Deterministic`,
`InvalidRobustnessSettingsAreInvalidConfiguration` and `BookkeepingOverheadIsSmall` are all unchanged
from `logs/02_controls_prefix.log`.

## 8. Sensitivity (R7)

`logs/08_R7_sensitivity.log`. Verification, not tuning — no parameter was chosen to make anything
pass.

* **12 healthy configurations** — meshes 4×4/6×6/8×8, tolerances 1e-5…1e-8, divergence windows
  5/10/20, start iterations 5/10/20: every one has detectors ON exactly equal to detectors OFF, with
  field difference **0.000e+00** in all cases (406/737/1093 iterations at tol 1e-6).
* **11 genuine runaways** — the same spread: all detected as `Diverging` with finite fields, firing
  at 10–42 iterations with growth 4.7e+03 … 1.1e+20.

So the corrected detector depends on neither a single mesh nor an exact residual trajectory.

## 9. Fresh focused regression (R8)

A4 harness (configure → rebuild every target → verify → execute), `logs/09_R8_regression.log`.
Library `143a1dda0680bc1d` (pre-fix `212141137733bffa`).

```text
CFDSolverTests            90 run,  90 pass, 0 fail    (pre-fix 87/87; +3 new sequences)
CFDSimpleTests           123 run, 122 pass, 1 fail    (pre-fix 123/121/2)
CFDThermalTests           99 run,  99 pass, 0 fail
CFDDiscretizationTests   169 run, 168 pass, 1 fail
CFDCaseIntegrationTests   63 run,  61 pass, 2 fail, 18 disabled
CFDTurbulenceTests       136 run, 136 pass, 0 fail
TOTAL                    680 run, 676 pass, 4 fail, 18 disabled
```

The four-suite A5/A6 checkpoint alone is **467 run, 464 pass, 3 fail, 18 disabled** — identical to
A5's post-migration state.

**All 4 failures were pre-registered, and there are 0 new ones:**

| failure | status |
| --- | --- |
| `SIMPLERobustnessTest.DivergenceStatus` | R4b — the `iterations == 20` assertion only; A6 **MIGRATE_POLICY**, not migrated here |
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | **U-H**, frozen (GRAD-002 history) |
| `MultiBlockProductionCase.CurvedChannelGridConvergence` | **U-H**, frozen (W8 history) |
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | **U-H**, frozen (W8 history) |

**Fixed by ROB-001:** `SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline`.

clang-format (`logs/10_clang_format.log`): the production file was already clean; the unit-test file
was reformatted, and `CFDSolverTests` rebuilt from the formatted source produced a **byte-identical
binary** (`5afdc070bfd3d244`) with 90/90 — incidental confirmation the reformat was semantics-free.

## 10. Files modified

```text
src/solver/SolverRobustness.cpp                  production -- 2 functional lines in diverging()
tests/unit/solver/test_solver_robustness.cpp     + 3 non-vacuity sequences (R5)
results/p12-diff-002/rob-001/                    gate, summary, logs 00-11, 2 probes
TODO.md                                          chronology
```

Production hashes: `SolverRobustness.cpp` `d210001a…` → `2b97f94a…`; `libcfdcore.a`
`212141137733bffa…` → `143a1dda0680bc1d…`. Everything else byte-identical, including
`SolverRobustness.hpp`, `test_simple_robustness.cpp`, all 15 other production numerical files, every
frozen gate, and A6's evidence (`logs/11_final_scope.log`).

## 11. Out of scope, untouched

M-E migration, the U-D MMS policy question, `DivergenceStatus`'s iteration assertion, U-C/U-F/U-H, W7,
W8, GRAD-002, MESH-007 and the MESH-004 ASan defect. No commit, no push.

## 12. Next decision

```text
RESUME A6 FROM STEP 1
```

Both M-E tests should be re-evaluated on the corrected library. On this evidence:

* `DefaultRobustnessPreservesBaseline` now **passes unchanged** — it was never an obsolete baseline,
  and A6's PRODUCTION_DEFECT classification is vindicated.
* `DivergenceStatus` still protects genuine divergence (status, finiteness, 1.66e+14 growth, detail),
  while its `iterations == 20` assertion remains unjustified — measured 20/20/21 across 4×4/6×6/8×8
  and 10–42 across the R7 sweep, against a derived lower bound of `startIteration + window = 20`.
