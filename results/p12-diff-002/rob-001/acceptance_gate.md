# P12-DIFF-002-ROB-001 — Divergence detector floor consistency — acceptance gate

Authorized 2026-09-17. Frozen **before any production modification**. Entry state:
`P12-DIFF-002 A6 BLOCKED — PRODUCTION DEFECT FOUND`; A6 is not authorized to continue until ROB-001
completes.

**Stop rule.** Stop at the first failed criterion. Never weaken a threshold, disable a test, delete an
assertion or reduce coverage to obtain a green result.

Baseline hashes: `logs/00_baseline_freeze.log`. A6 and all earlier evidence are preserved unchanged
and re-verified there; nothing in `a6/`, `a5/`, `thermal-interface-fix/` or any earlier gate is
rewritten.

---

## 1. The defect

`src/solver/SolverRobustness.cpp::OuterIterationMonitor::diverging` applies two rules to the same
residual tracker:

```text
RULE 1  (lines 389-400)   base = std::max(tracker.floor(), tracker.bestBeforeWindow())
                          fires when  windowMin >= growthFactor * base
RULE 2  (lines 401-415)   oldest = tracker.valueAgo(window - 1)            <-- raw, NO floor
                          fires when  the window increases strictly at every step
                                      AND  valueAgo(0) >= growthFactor * oldest
```

`tracker.floor()` **is** the convergence tolerance for that residual — line 356 returns it as the
absolute convergence threshold. Rule 1 therefore cannot fire on a residual that is already converged;
rule 2 can, and does.

Consequence, measured on `cavity(4, 0.01)`, α = 0.7/0.3, tol 1e-6 (`logs/01_prefix_reproducer.log`):
the reference run **Converges** in 406 iterations, and merely *enabling* the detectors aborts the same
solve at iteration 112 as **Diverging** with an unconverged field. The trip is a monotone round-off
drift of the continuity residual, `1.9578e-14 → 3.1002e-13` — values 5.1e+07× and 3.2e+06× *below*
the 1e-6 tolerance, with every one of the ten window values already converged, while the u residual
decreases monotonically throughout.

## 2. The intended correction

Inside `diverging()` only: apply the **same** meaningful-residual floor rule 1 already uses when rule
2 evaluates its historical/oldest residual —

```text
meaningful_oldest = std::max(tracker.floor(), oldest)
```

Nothing else. `SolverRobustness` is not redesigned; growth factors, windows, start iterations,
convergence tolerances, the stagnation rule, the fallback rule, the adaptive-relaxation controller and
the non-finite path are all untouched. No case, mesh or iteration number is special-cased, and rule 2
is not disabled.

Note that flooring the oldest value also makes the newest exceed the floor implicitly, since firing
then requires `newest >= growthFactor * floor > floor`; no second change is needed.

## 3. Criteria

Each row records the **pre-freeze dry-run** status against the unmodified library, so no criterion is
frozen unverified and none can pass vacuously.

| id | criterion | threshold | dry-run against the unmodified library |
| --- | --- | --- | --- |
| **R1** | **Reproducer first.** An independent reproducer of the 4×4 cavity shows detectors-OFF vs detectors-ON differing, and replays both rules on **every** tracked residual (u, v, pressure, continuity) | must FAIL before the fix; rule 1 fires nowhere; rule 2 **unfloored** fires on continuity at 112; rule 2 **floored** fires nowhere | **confirmed**: OFF Converged/406, ON Diverging/112, field diff u 7.98e-04; rule 2 unfloored fires@112, floored does not fire on any residual |
| **R2** | **Minimal production change.** Only `src/solver/SolverRobustness.cpp` differs from `logs/00`, and only inside `diverging()`. `SolverRobustness.hpp` and every other production file byte-identical. No change to `growthFactor`, `window`, `startIteration`, any tolerance, the stagnation rule, the fallback rule, the adaptive controller or the non-finite path | by hash + reviewed diff; the diff must not exceed what §2 describes | — |
| **R3** | **Baseline invariance.** `SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline` passes with its file **byte-identical** to `97fc7b61…`; and directly measured, detectors OFF vs ON give the same status, the same iteration count and a field difference of **exactly 0** | pass; field difference `== 0.0` exactly | satisfiable: with the floor applied no rule fires on the reference trajectory, and **stagnation would not fire either** (worst windowed improvement 6.478e-01 vs the 1e-2 threshold) |
| **R4** | **Genuine divergence still fires.** The genuine runaway of `SIMPLERobustnessTest.DivergenceStatus` still reports `Diverging`, with finite fields, `final u / first u` ≥ 1e3 and a `statusDetail` naming divergence. The **exact** historical `iterations == 20` is *not* required and that assertion is *not* amended | status, finiteness, growth and detail all hold | the runaway's residuals are 1.09e+04 … 1.26e+13, far above any floor, so rule 1 and rule 2 both retain their base values |
| **R4b** | **Pre-registered known failure.** `DivergenceStatus` is expected to keep failing on its single `EXPECT_EQ(result.iterations, 20u)` assertion, which A6 classified **MIGRATE_POLICY** and which ROB-001 is forbidden to migrate. That is **not** a ROB-001 regression and must be the **only** assertion failing in it | exactly that one assertion | measured pre-fix: 21 vs 20, every other assertion in the test passes |
| **R5** | **Direct rule-2 non-vacuity**, added as a new unit test in `tests/unit/solver/test_solver_robustness.cpp`, driving the monitor directly with three sequences on the **continuity** channel (u/v/pressure held above tolerance so the sample is not converged, exactly as production reaches the rule): (a) healthy decreasing → no fire; (b) sub-floor monotone round-off growth → **no** fire; (c) above-floor monotone runaway → **fires** | all three, and (c) must fire *through rule 2* | (b) is the defect, so it fires pre-fix and must stop; (c) is the power case. The existing `ResidualTrend.SustainedRunawayFromAPlateau` is the built-in above-floor rule-2 control (oldest 1e-3 vs floor 1e-6) and must keep passing |
| **R6** | **The rest of `SolverRobustness` is unchanged.** All 10 `SIMPLERobustnessTest` cases and the whole `CFDSolverTests` suite — real divergence, stagnation, non-finite detection, fallback/recovery, healthy convergence, determinism, settings validation, bookkeeping cost — behave as recorded in `logs/02_controls_prefix.log`, except the one false-positive path | `CFDSolverTests` 87/87; `SIMPLERobustnessTest` 9 of 10 pass with only R4b's assertion failing | pre-fix: `CFDSolverTests` 87/87 pass; `SIMPLERobustnessTest` 8 pass, 2 fail (`DefaultRobustnessPreservesBaseline`, `DivergenceStatus`) |
| **R7** | **Sensitivity, not tuning.** The corrected detector is exercised across meshes (4×4, 6×6, 8×8), convergence tolerances and divergence windows: no healthy run is aborted, and a genuine runaway is still detected in every configuration | 0 false positives; genuine runaway detected in every configuration | — |
| **R8** | **Fresh focused regression** from a forced rebuild with recorded library and binary hashes: `CFDSimpleTests`, `CFDSolverTests`, then the A5/A6 four-suite checkpoint `CFDThermalTests`, `CFDDiscretizationTests`, `CFDCaseIntegrationTests`, `CFDTurbulenceTests` | exact counts; **0 new unexplained failures**; the only permitted failures are R4b's assertion and the 3 **U-H** tests already frozen | pre-fix four-suite checkpoint: 467 run, 464 pass, 3 fail (all U-H) |
| **R9** | **Scope.** Untouched: the ThermalInterface fix, the A5-migrated tests, the U-D MMS tests, U-C/U-F/U-H, W7, W8, GRAD-002, MESH-007, the MESH-004 ASan defect, and `test_simple_robustness.cpp` | by hash | — |

## 4. Out of scope

M-E migration and the U-D MMS policy question are **not** part of ROB-001 — A6 resumes separately.
`DivergenceStatus` is not migrated here. No commit, no push, no other P12 phase.

## 5. Verdicts

```text
R3 fails (detectors still alter a healthy run)   -> P12-DIFF-002-ROB-001 BLOCKED — FALSE POSITIVE REMAINS
R4 or R5(c) fails (runaway no longer detected)   -> P12-DIFF-002-ROB-001 BLOCKED — GENUINE DIVERGENCE DETECTION REGRESSED
any other criterion fails                       -> P12-DIFF-002-ROB-001 BLOCKED — REGRESSION FOUND
R1-R9 all pass                                   -> P12-DIFF-002-ROB-001 COMPLETE — READY TO RESUME A6
```
