# P12-DIFF-002 Amendment A6 — Step 1 (M-E investigation) — report

**Status: `P12-DIFF-002 A6 BLOCKED — PRODUCTION DEFECT FOUND` at Step 1.**

Step 1 is investigation-only and its own stop rule fired: *"If `PRODUCTION_DEFECT` → STOP."* No A6
gate was frozen (Step 4 is conditional on Steps 1–3 establishing that the failures are
validation-policy defects, which Step 1 disproves for one of the two tests), no test was amended, and
Steps 2–10 were not started. Production was read but **not modified**.

Evidence: `a6/logs/01_ME_current_failures.log`, `a6/logs/02_ME_investigation.log`,
`a6/logs/03_divergence_rule_replay.log`, `a6/tools/a6_me_probe.cpp`,
`a6/tools/a6_divergence_replay.cpp`.

---

## 1. Classifications

| test | classification |
| --- | --- |
| `SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline` | **PRODUCTION_DEFECT** |
| `SIMPLERobustnessTest.DivergenceStatus` | **MIGRATE_POLICY** |

Neither was amended. The first is the reason A6 stops.

## 2. `DefaultRobustnessPreservesBaseline` — PRODUCTION_DEFECT

**Original purpose.** `tests/solver/simple/test_simple_robustness.cpp:178`. P12-NUM-004's
backward-compatibility guarantee: with the default robustness settings — *and* with the detectors
merely enabled on a case that should not need them — SIMPLE must reproduce the pre-P12-NUM-004 solver
**bit for bit**. It is not an iteration-count baseline at all; the count it reads
(`d.uNormalizedHistory.size() == reference.iterations`) is self-referential.

**Exact failing assertions.**

```text
:126  ASSERT_EQ(a.status, b.status)              reference 0 (Converged) vs guarded 8 (Diverging)
:196  EXPECT_TRUE(guarded.robustness.statusDetail.empty())   actual: false
```

**Measured.** `cavity(4, 0.01)`, α = 0.7/0.3, tol 1e-6, maxIterations 500:

| | reference (no detectors) | guarded (detectors enabled) |
| --- | --- | --- |
| **current (DIFF-002)** | Converged, 406 it, final u 3.03e-08 | **Diverging, 112 it, final u 1.70e-05** |
| **pre-DIFF-002** | Converged, 390 it, final u 2.91e-08 | Converged, 390 it, final u 2.91e-08 |

Field difference reference-vs-guarded: **0.000e+00 pre-DIFF-002**, and **u 7.98e-04, v 5.61e-04,
p 7.52e-04** now. So enabling a robustness detector now aborts a converging solve and returns an
**unconverged field** (u residual 1.70e-05 against a 1e-6 tolerance) labelled `Diverging`.

**Root cause, localized and independently replayed.**
`src/solver/SolverRobustness.cpp::OuterIterationMonitor::diverging` applies two rules to the same
tracker:

* **Rule 1** (lines 389–400) — floored: `base = std::max(tracker.floor(), tracker.bestBeforeWindow())`
  and fires when `windowMin >= growth * base`. `floor()` **is the convergence tolerance** (line 356
  returns it as the absolute convergence threshold). Rule 1 therefore cannot fire on a residual that
  is already converged.
* **Rule 2** (lines 401–415) — **not** floored: `oldest = tracker.valueAgo(window - 1)` is used raw,
  and it fires when the window increases strictly at every step **and** `newest >= growth * oldest`.

`a6/logs/03_divergence_rule_replay.log` replays both rules on the reference run's own continuity
history:

```text
RULE 1 (floored, as production applies it)     does NOT fire
RULE 2 as production applies it (NO floor)     fires at iteration 112   (1.9578e-14 -> 3.1002e-13)
RULE 2 with the same floor rule 1 uses         does NOT fire
RULE 1 / RULE 2 on the u-residual history      neither fires
```

The replay reproduces production's own `statusDetail` exactly — *"the continuity residual increased at
each of the last 9 iterations, 1.958e-14 -> 3.1e-13 (>= 10x)"*. The two values that trip it are
**5.1e+07×** and **3.2e+06× below** the 1e-6 continuity tolerance; every continuity residual from
iteration 100 to 117 is already converged. Meanwhile the u residual decreases monotonically
throughout (5.33e-02 → 1.45e-05).

**Why this is a defect and not a policy matter.** The same function floors one rule and not the other,
so a residual the detector itself classifies as converged can still be read as diverging. The
consequence is not cosmetic: a converging solve is aborted and a wrong answer returned under a
`Diverging` status. DIFF-002 did not create the defect — pre-DIFF-002 the guarded and reference runs
were bit-identical — it exposed it by changing the trajectory so the continuity residual now drifts
monotonically upward at round-off level for nine consecutive iterations.

**Therefore the test is correct and must not be migrated.** Its assertion — that enabling the
detectors does not change the outcome on a case that does not need them — is exactly the property
that is now violated. This is the same lesson as A5's entry 8, in a second inventory class: *a failing
test in a "known-obsolete baseline" list is not thereby obsolete.*

**Smallest correction — NOT implemented.** Apply rule 1's floor to rule 2, i.e. compare against
`std::max(tracker.floor(), oldest)` (and, for symmetry, require the newest value to exceed the floor
at all). That is a one-line change inside the same function, it leaves the genuine-runaway path
untouched (there the residuals are 10⁴–10¹³, far above any floor), and it restores the pre-DIFF-002
bit-identical behaviour on this case. It was deliberately not made: Step 1 is investigation-only and
authorizes no production change.

## 3. `DivergenceStatus` — MIGRATE_POLICY

**Original purpose.** `test_simple_robustness.cpp:296`. A genuine finite runaway must be stopped with
finite fields and reported as `Diverging`. Its comment asserts the detector stops it "at the earliest
possible iteration (startIteration 10 + window 10 = 20)".

**Exact failing assertion.** `:304 EXPECT_EQ(result.iterations, 20u)` — actual **21**. Everything else
in the test passes: status `Diverging`, fields finite, growth `final u / first u = 1.66e+14` (bound
1e3), `statusDetail` contains "divergence".

**Is the count a contract?** No. With `startIteration = 10` and `window = 10`, iteration 20 is the
*earliest the detector can possibly fire* — a lower bound, not a predicted value. Whether it fires at
20 or later depends on whether *every* one of the last ten residuals is ≥ 10× the best since
iteration 10, and the runaway is non-monotone (history shows [16] 4.23e+09 → [17] 2.94e+09 and
[18] 2.61e+11 → [19] 9.16e+10), which is precisely why 20 is missed and 21 fires.

**Sensitivity** (`a6/logs/02_ME_investigation.log`): the fire iteration is mesh- and
operator-dependent, never a constant —

| mesh | current | pre-DIFF-002 |
| --- | --- | --- |
| cavity 4×4 | 20 | 25 |
| cavity 6×6 | 20 | 21 |
| cavity 8×8 | **21** | 20 |

The solver's own iteration counts are also tolerance-sensitive (4×4 converges in 296/406/500 at tol
1e-5/1e-6/1e-7, hitting `MaxIterations` at 1e-7 now versus 496 iterations pre-DIFF-002).

**Numerical solution / conservation / accuracy:** unchanged in character — this case is *designed* to
diverge, and it still does, from 7.54e-02 to 1.26e+13 with finite fields. Without the detector it runs
to 150 iterations and u 1.12e+102, so the detector is still doing its job.

**Status assertion independent of the count:** yes. `status == Diverging`, finiteness, the ≥1e3 growth
and the `statusDetail` content are all meaningful on their own and all pass.

**Would-be amendment (not made, since A6 stopped):** replace the exact `20` with the derived
contract — `iterations >= startIteration + window` (20, the earliest possible) together with an
upper performance envelope, keeping every other assertion. No derivation adopts the current output as
the expectation: 20 remains in the criterion as the mathematically derived lower bound.

## 4. What was NOT done

Steps 2–10 were not started: no U-D/MMS investigation, no theoretical MMS derivation, **no A6 gate
frozen**, no test amended, no W7/W8/W9/W10. W8, the GRAD-002 gradient-order test, MESH-007 G6.3, the
13 **U** tests and the MESH-004 ASan defect are untouched. No production file was modified.

## 5. Recommended next authorization

A focused fix for the divergence detector's unfloored rule, gated the way the ThermalInterface fix
was: freeze the gate first; add a reproducer that fails on the current library (the 4×4 cavity above
is one, and `DefaultRobustnessPreservesBaseline` already is one); apply rule 1's floor to rule 2;
require the genuine-runaway tests (`DivergenceStatus`, `AdaptiveRelaxationRecovery`, the stagnation
and fallback cases) to keep detecting what they detect; and only then revisit M-E and the U-D MMS
policy question.
