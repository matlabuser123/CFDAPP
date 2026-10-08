# TD-005 — ACCEPTANCE GATE — flame-speed extraction method

```text
item        TD-005   Flame-speed extraction method not frozen · P1
authorized  2026-10-04, "do TD-005 next"
audit       audit.md (read first — it names what is already established and must not be redone)
baseline    logs/01_reproducibility_baseline.log, measured on the UNCHANGED current tree
status      FROZEN. Section 6's dry-run is complete for every criterion; section 7 records the
            hash and logs/00_freeze.log records the tree it was frozen against.
scope       This gate covers the WHOLE item. The authorized change in hand is C4d only ("fix the
            report"); C3 and C6 remain open and fixing C4d does not close TD-005.
```

---

## 1. What this gate is for

The register's requirement is that **the method be frozen before the answer is looked at**. So this
gate has an unusual shape for this campaign: its first criterion is that the *definition* is fixed
and self-consistent, and the remaining criteria verify the claims that definition rests on. The
audit established that the method is already designed and already correct in its two hardest
respects (the degeneracy exclusion and the plateau mechanism), so **this gate does not re-derive the
method**. It closes four measured gaps:

```text
F1  the insensitivity sweeps assert on S_L ALONE, and one configuration returns a state
    with a 37 % fuel-conservation violation while the test passes          -> C3
F2  the PLATEAU -- the mechanism that selects the eigenvalue -- has never been
    measured. Its test was recorded TIMEOUT, but at a 900 s cap that is TD-004's
    HARNESS, not this repository's: see 4.4                                -> C4
F3  the consumption-identity test FAILS on a bundled TD-003 assertion, not
    on the identity, which reads 0.00777 against its own 0.01 bound        -> C6
F4  relaxationTimesPerTrial, the knob introduced to fix the recorded 4.3 %
    blocker, is swept by no test anywhere in the tree                      -> C3, sweep C
```

## 2. Why the recorded 4.3 % blocker is not this gate's headline

`flame_solver_findings.md` §3.1 records 4.3 % reproducibility and requires `<< 2.8 %`. TD-004's
baseline measures 0.068 % on the current tree, and CLAUDE.md §8 says a known failure stays known only
while it reproduces its recorded values. **If `logs/01` confirms it does not reproduce, the precision
precondition is met and that is recorded as a resolved precondition, not claimed as this item's
achievement** — `relaxationTimesPerTrial` fixed it, before TD-005 was authorized. The gate still
measures it (C2), because a precondition asserted from a pre-fix log is not a measurement.

## 3. Criteria, in execution order

| # | criterion | instrument | threshold | derivation |
| --- | --- | --- | --- | --- |
| **C1** | **the reported quantities are internally exact** | probe, every configuration | `flameSpeed == massFlux / unburntDensity` **bitwise**; and when `status == Converged`, `massFluxResidual <= tolerance` | §4.1. Zero tolerance: recomputing one division from two reported doubles must give the identical double. The residual bound is the loop's own exit condition, so a violation is a reporting defect |
| **C2** | **the bracket bounds the eigenvalue, and `S_L` converges as it tightens** | probe sweep D, `tolerance` ∈ {1e-2, 1e-3, 1e-4} | `|S_L(1e-3) − S_L(1e-2)|/S_L <= 5.49e-03` **and** `|S_L(1e-4) − S_L(1e-3)|/S_L <= 5.50e-04` | §4.2. `mdot*` is the bracket's geometric centre, so its relative error is at most `sqrt(1+t) − 1`; two answers differ by at most the sum of their two bounds. **No fitted constant** |
| **C3** | **every configuration in an insensitivity sweep returns an ADMISSIBLE state** | probe sweeps A, B, C — **at every level, not only the reference** | `fuelBalanceResidual < 0.05` at **every** level | §4.3. This is the repository's **own existing** precondition bound, from `TheConsumptionIntegralAgreesWithTheEigenvalue`, neither weakened nor tightened. F1 is a coverage gap, not a threshold problem |
| **C4** | **the plateau is demonstrated, not assumed** | the plateau test, run at the **repository's** cap | (a) it produces a **verdict** under `ctest --timeout 7200`; (b) `S_L` spread over `anchorFraction` {0.30, 0.40, 0.50} `<= 0.02`; (c) `|S_L(0.50) − S_L(0.40)| <= |S_L(0.40) − S_L(0.30)|` | §4.4, **corrected**. (b) is the existing in-tree threshold, unchanged. (c) is the **definition** of a plateau — successive differences non-increasing — and carries no constant at all |
| **C4d** | **the reported front position is MEASURED, not an echoed input** | the field, at every level of sweeps A–C | `result.flamePosition` must equal the interpolated `T_anchor` crossing of the reported temperature field to within **one cell** (`dx`); and the plateau test's front clause must compare it against the **field**, not against `anchorFraction * domainLength` | §4.7 — added during the dry-run after `logs/02` found `PremixedFlame.cpp:1725` assigns `xAnchor`. One cell is the resolution of a linearly interpolated crossing, so it is the quantity's own floor |
| **C5** | **the controls behave** | **TWO** constructed controls, not three — see §4.5 | unconverged → `status == MaxIterations` **and** `massFluxResidual > tolerance`; no flame → `status == NoFlame`, reachable **only** via `reactionsEnabled = false` and **NOT YET MEASURED** at the repository cap | §4.5, **corrected during the dry-run**. Status equality is exact. "No flame" and "reactions disabled" are the SAME control on this solver |
| **C6** | **the consumption identity is reported on its own** | the split test | the identity assertion is evaluated and reported independently of the `ΣY` bound; the `1e-8` `ΣY` bound is **kept at its current value** and attributed to TD-003 | §4.6. The `ΣY` bound is a **real defect detector**, so it is not migrated, loosened or removed — it is stopped from masking a different result |
| **C7** | **no silent change elsewhere** | `CFDSpeciesTests` excluding `G8PremixedFlameTest` | **343**, unchanged | TD-004's measured baseline for the same suite and the same exclusion |

**Execution stops at the first failure.** Classifying a failure does not waive it.

## 4. Derivations

### 4.1 C1 — why bitwise, and why that is not pedantry

`PremixedFlame.cpp:1515` computes `result.flameSpeed = massFlux / rhoUnburnt`, and the result struct
reports `massFlux` and `unburntDensity` separately. Recomputing that single division from the two
reported fields must reproduce the reported `flameSpeed` **exactly** — same operands, same operation,
IEEE-754 deterministic. Any difference means a reported field is not the one that produced the
answer, which is the defect class the final march was introduced to fix (a profile that did not
correspond to the quoted eigenvalue). Zero is therefore the only defensible threshold, and it is an
exact control in the sense of CLAUDE.md §5.1.

### 4.2 C2 — the bracket bound, derived

The search exits with a bracket `[low, high]` satisfying `(high − low)/low <= t` and reports
`mdot* = sqrt(low · high)`. If the true eigenvalue `mdot_e` lies in the bracket — which it does when
the drift signs are correct, since the bracket is only ever narrowed on a sided trial — then

```text
mdot*/low  = sqrt(high/low) <= sqrt(1 + t)
mdot*/high = sqrt(low/high) >= 1/sqrt(1 + t)

=>  |mdot* − mdot_e| / mdot_e  <=  sqrt(1 + t) − 1
```

so two runs at tolerances `t_1` and `t_2` differ by at most the sum of their bounds:

```text
t = 1e-2  ->  sqrt(1.01)   − 1 = 4.98756e-03
t = 1e-3  ->  sqrt(1.001)  − 1 = 4.99875e-04
t = 1e-4  ->  sqrt(1.0001) − 1 = 4.99988e-05

C2 bound, 1e-2 vs 1e-3 :  4.98756e-03 + 4.99875e-04 = 5.48744e-03  ->  5.49e-03
C2 bound, 1e-3 vs 1e-4 :  4.99875e-04 + 4.99988e-05 = 5.49874e-04  ->  5.50e-04
```

**The stated assumption, recorded because the criterion is void without it:** the bound holds only if
every sided trial signed correctly. A C2 failure therefore has two readings — a mis-signed trial, or
a bracket that does not contain the eigenvalue — and both are real defects worth stopping for. C2 is
not a tolerance that can be relaxed; relaxing it would be asserting that the reported
`massFluxResidual` is not a bound, which contradicts what the result struct documents it to be.

### 4.3 C3 — the admissibility bound, and a criterion REJECTED during this derivation

`fuelBalanceResidual < 0.05` is taken from the tree, at `test_premixed_flame.cpp:447`:

> `ASSERT_LT(flame.fuelBalanceResidual, 0.05) << "the state does not conserve fuel well enough for
> the consumption identity to mean anything, so the mismatch below would be measuring the wrong
> state"`

That is precisely the judgement C3 needs, already written and already justified in the tree. F1 is
that it is applied **once, at the reference configuration**, while the sweeps that certify
insensitivity apply nothing. C3 changes the **coverage**, not the number — so there is no threshold
to weaken or tighten, and the comment above is the derivation.

**A criterion considered and REJECTED, recorded because it would have been vacuous.** The obvious
admissibility test is the consumption identity itself, bounded by the quantities that limit it:

```text
REJECTED:  |S_c − S_L|/S_L  <=  fuelBalanceResidual + massFluxResidual
```

It is dimensionally sound and looks derived. It is also **worthless**, and the baseline already shows
why — at the bad configuration:

```text
drift window 16:  |S_c − S_L|/S_L = 0.3737      fuelBalanceResidual + bracket = 0.3743
                  -> the REJECTED criterion PASSES on the 37 %-violating state
```

The mismatch and the fuel-balance residual are **the same measurement wearing two faces**: the
consumption identity *is* the integrated fuel equation, so bounding one by the other is an identity,
not a test. TD-004's gate found the same trap between the fuel and energy balances (`e/f = 1.00000`,
"the two independent balances are one measurement"); this is its third face. **A criterion that both
the good and the bad state satisfy discriminates nothing**, so C3 bounds the **absolute** level of
the balance instead.

### 4.4 C4 — the plateau, and why (c) carries no constant

(b) is `test_premixed_flame.cpp:406`'s existing `0.02`, unchanged. (c) is new and is the better of
the two, because it tests the **shape** the physics predicts rather than a magnitude: the
cold-boundary correction decays as the front moves away from the inlet, so successive differences
must not grow. With `delta_preheat = 1.229 mm` the three anchors sit at `2.44 / 3.26 / 4.07`
thicknesses from the inlet, so each step downstream must matter less than the one before. A rising
difference means the answer is still tracking the anchor and no magnitude bound would reveal it.

Exact equality satisfies (c) and is the ideal plateau; (c) is `<=`, not `<`.

### (a) — A CORRECTION MADE DURING THIS DRY-RUN, before the freeze

The first draft of this gate treated the plateau test as timing out against a **900 s** cap and
specified real engineering work to fit inside it. **That cap is not this repository's.** Measured:

```text
tests/unit/species/CMakeLists.txt:37   gtest_discover_tests(CFDSpeciesTests ...)
                                       -> EVERY gtest case is its own ctest entry
.github/workflows/ci.yml:147           ctest ... --timeout 7200
grep -rn TIMEOUT tests/**/CMakeLists.txt CMakeLists.txt cmake/Testing.cmake
                                       -> no per-test TIMEOUT property anywhere
TD004_premixed_flame/logs/06, line 5   "per-test timeout 900s"   <- TD-004's HARNESS
```

So the repository's cap is **7200 s per gtest case**, and the plateau test's ~1000 s sits well
inside it. The three recorded "TIMEOUT after 900s" verdicts are **artefacts of TD-004's probing
harness**, not repository test failures, and TD-004's log says so in its own header.

**Consequence for this gate:** the plateau may already be measurable with no code change at all, by
running the test at the repository's own cap. (a) is therefore specified as *produce a verdict under
`--timeout 7200`*, and the engineering work the first draft assumed may not exist. What is NOT
weakened: (b) and (c) still have to pass on whatever that verdict contains.

**Still forbidden, if (a) does turn out to need work:** raising the repository cap, cutting the
number of anchors, dropping the domain-length check, or coarsening the mesh to make it fit — each
makes the test finish without making it work. Splitting one test into two that measure exactly the
same things is permitted, because it changes the cap each part faces and nothing else.

**This correction is recorded rather than quietly applied**, because a gate written around a cap
that is not the repository's would have measured the wrong thing, and §5.2's non-runner list was
derived from the same mistake.

### 4.5 C5 — the controls, and THERE ARE TWO OF THEM, NOT THREE

```text
unconverged     maxOuterIterations = 1: one bisection cannot narrow a 2x bracket to 1e-3
                -> MaxIterations AND massFluxResidual > tolerance   (exact; BOTH required)
                MEASURED (logs/05): MaxIterations, massFluxResidual = 4.1421e-01, bisections 1.
                4.1421e-01 is sqrt(2) - 1 exactly -- the half-bracket after one geometric
                bisection -- so the control lands on the value 4.2's derivation predicts.

no flame        reachable ONLY via reactionsEnabled = false, and NOT YET MEASURED.
```

The unconverged control requires **both** the status and the residual, because a status is one enum
assignment and a criterion resting on a single assignment is weaker than one resting on the
assignment and the quantity that justifies it. This item has already found a reported field that was
an echoed input (§4.7), so an enum on its own is not evidence.

#### 4.5.1 A CORRECTION FROM THE DRY-RUN: the register's three controls are two

The first draft constructed the no-flame control by putting `T_anchor` above `T_ad`
(`anchorTemperatureFraction = 1.5`). **The solver refuses that input:**

```text
cfd::InvalidArgumentError: solvePremixedFlame: anchorTemperatureFraction is 1.5;
                           must be within (0, 1)
```

which is correct behaviour, and it means `T_anchor` can **never** exceed `T_ad`, so that route to a
flame-free domain does not exist. The refusal is a control in its own right, but it is an
**input-validation** control already covered by `MalformedConfigurationsAreRefused`.

Routes considered and **rejected as confounded**: removing the fuel drives `T_ad` to `T_u`, which
makes `T_anchor = T_u` and `flameThickness` a `0/0` — that tests the degenerate case, not the
no-flame case, and moves several things at once.

**So the register's "FAIL with no flame" and "FAIL with reaction disabled" are the SAME control on
this solver**, and it is the one TD-020 has made expensive. C5 therefore has **two** distinct
controls, of which **one is demonstrated** (unconverged) and one is **unmeasured**
(`WithReactionsDisabledNoFlamePropagates`, to be run at `--timeout 7200` by
`tools/run_noflame_control_at_repo_cap.sh`).

**C5 cannot be reported as PASS until that run exists.** The failed first attempt is preserved at
`logs/04_controls_dryrun_INVALID_construction.log`.

#### 4.5.2 An overstatement in the probe's own summary line, recorded

`logs/05` ends with *"ALL CONTROLS BEHAVED. C5's non-vacuity is demonstrated for 1 and 2."* That
line is **wrong**: control 1 is the validation refusal, not the no-flame control, and the probe's own
control-3 paragraph says so three lines earlier (*"C5 has exactly ONE demonstrated control"*). The
body is right and the summary line overstates. Recorded rather than re-run, because the numbers are
sound and the discrepancy is in a printed sentence, not in a measurement.

### 4.6 C6 — keep the detector, stop it masking

The audit established that the consumption-identity test fails on
`maxCompositionSumDeviation < 1.0e-8` (actual `6.29e-07`) while the identity itself passes at
`0.00777 < 0.01`. CLAUDE.md §5.3's three-way distinction applies, and the answer is the third:

```text
an obsolete CONSTANT?      no -- 1e-8 is a real closure requirement, not a stale number
a contaminated INSTRUMENT? no -- the property is measured where it should be
a REAL DEFECT DETECTOR?    YES -- it detects TD-003's tolerance-limited inner solve
```

So the rule is "do not migrate it; report the defect". C6 therefore **keeps `1e-8` exactly as it is**
and only stops it from making a different, passing result read as a failure. Loosening it to make
the suite green would be weakening a threshold to obtain a PASS.

### 4.7 C4d — why a criterion had to be ADDED during the dry-run

`logs/02` established that `result.flamePosition` is `anchorFraction * domainLength` echoed back
(`PremixedFlame.cpp:1725`, the sole assignment), and that the plateau test's front clause
(`test_premixed_flame.cpp:400`) compares that input against the same expression — an identity that
cannot fail. In the bad mode the real crossing is **48.2 cells** off the anchor and the reported
value is the anchor.

So the gate as drafted would have frozen a criterion set in which the front position — the quantity
the entire anchoring mechanism exists to control — was unchecked. C4d closes that.

**Threshold derivation.** The front is a linearly interpolated `T_anchor` crossing between two cell
centres, so its own resolution is one cell; a reported position agreeing with the field to better
than `dx` is as good as the definition allows. **No fitted constant**, and the good mode already
demonstrates the achievable value (`0.3` cells).

**Non-vacuity is proved in both directions by `logs/02` itself:** the good mode satisfies C4d
(`0.3 <= 1` cell) and the bad mode violates it (`48.2`), with no construction required.

**What C4d must NOT become.** Fixing the report must not change any computed answer — `flamePosition`
is an output, not an input to the march. So C4d carries a companion requirement: after the fix,
`S_L`, `massFlux`, `fuelBalanceResidual` and `consumptionSpeed` must be **bitwise unchanged** at the
reference configuration. If they move, the field was feeding something it should not.

## 5. Scope and named non-runners

### 5.1 Regression scope

| suite | expectation |
| --- | --- |
| `CFDSpeciesTests` excluding `G8PremixedFlameTest` | **343**, unchanged |
| `G8PremixedFlameTest` | reported **test by test**, before and after. Several currently FAIL or TIME OUT; each one's movement is accounted for individually, because a total would hide one improving while another degrades |
| `CFDIoTests`, `CFDAppLayerTests` | **331 / 32**, unchanged — the flame reaches neither |

### 5.2 NAMED, PERMITTED NON-RUNNERS

CLAUDE.md §8 permits only the failures a gate names — **and a gate may not name a failure it has
not measured at the cap it is claiming.** §4.4(a) establishes that the baseline's three "TIMEOUT"
verdicts were taken at TD-004's 900 s harness cap, not at the repository's 7200 s. So:

**MEASURED at the repository cap (`logs/03`), and the result is that two of the three recorded
"timeouts" were never repository failures:**

```text
TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength   PASS   997 s   <- C4(a) SATISFIED
    anchor plateau 4.85572 .. 4.86230 cm/s, spread 0.001355
    IT MISSED TD-004's 900 s HARNESS CAP BY 97 SECONDS. That 97 s produced a "TIMEOUT"
    verdict which became "the plateau has never been measured" in two items' evidence.
TheFlameSpeedConvergesUnderMeshRefinement                    PASS   301 s   (reported, not gated)

MEASURED FAILURES, named and permitted (verdicts, not caps):
  TheConsumptionIntegralAgreesWithTheEigenvalue   FAIL         on the TD-003 bound -> C6
  ItConvergesAndReachesTheIndependentlyDerived... FAIL         TD-004 territory
  AVariableConductivityChangesTheAnswer...        FAIL   0 s   property 8, not this gate

STILL NOT MEASURED AT THE REPOSITORY'S CAP:
  WithReactionsDisabledNoFlamePropagates          -> C5's no-flame control, TD-020
```

**C4(a) therefore requires NO code change.** The engineering work the first draft assumed for it does
not exist; what remains in C4 is C4d. **A caution that belongs here:** the plateau test passes
*while including* anchor 0.30, whose fuel balance is `0.3571`. It passes because it checks the `S_L`
spread and the front-position identity, neither of which can see an inadmissible state. "The plateau
is verified" and "C3 fails at anchor 0.30" are both true, and the second is invisible to the test
that establishes the first.

**No OTHER test may acquire a new failure, and none may exceed `--timeout 7200`.** The first draft
of this clause called the headroom "thin" on the basis of the 900 s figure; against 7200 s the
longest baseline test (804 s) has roughly **9x** headroom, so TD-004's ~2x locator cost is not a
timeout risk. That mis-statement is corrected here and in TD-004's own summary §4.3.

### 5.3 Explicitly NOT in this gate

methane, `cp`, `lambda(T)` and any absolute `S_L` claim (**TD-006** — debugging against methane
would mean deciding with G8's gated answer visible); the flashback and the march-length balance
degradation (**TD-004**, stopped and preserved); the `1e-9`-limited inner solve (**TD-003**); the
no-flame early-out (**TD-020**); route (b)'s drifting-frame reformulation; mesh convergence; any
performance claim.

## 6. Dry-run against the unchanged baseline

**MANDATORY BEFORE FREEZING** (CLAUDE.md §5.2). Every criterion must be run against the unchanged
tree, and each must be shown able to **both** pass and fail.

**COMPLETE. Every criterion was executed against the unchanged tree before this gate was frozen.**

| # | measured on the unchanged tree | non-vacuity | log |
| --- | --- | --- | --- |
| C1 | **PASS** — bitwise in both the good and the bad mode | corrupt either reported field | `logs/02` |
| C2 | **PASS** — brackets `5.43e-03/6.77e-04/8.46e-05` all `<=` requested; `|ΔS_L|` `1.017e-03` and `2.962e-04` against `5.49e-03` and `5.50e-04` | control 2 exercises the FAIL side: `massFluxResidual 4.1421e-01 > 1e-3` | `logs/01`, `logs/05` |
| **C3** | **FAIL at 5 of 12 configurations** (`0.2380 … 0.3721` against `< 0.05`) | **both directions by the baseline alone** — 7 pass, 5 fail, same criterion, nothing constructed | `logs/01` |
| C4a | **PASS** — 997 s against the 7200 s cap. No code change needed | a genuine timeout would fail it | `logs/03` |
| C4b | **PASS** — `0.001355 <= 0.02` | a spread above `0.02` fails | `logs/01`, `logs/03` |
| C4c | **PASS** — exactly `0` against `6.77e-04` | a growing successive difference fails | `logs/01` |
| **C4d** | **FAIL at the bad mode** (`48.2` cells against `<= 1`); **PASS** at the good mode (`0.3`) | **both directions by `logs/02`**, nothing constructed | `logs/02` |
| C5 | **1 of 2 controls demonstrated.** Unconverged: `MaxIterations` **and** `4.1421e-01 > 1e-3`, both clauses PASS. No-flame: **UNMEASURED** | positive control passes (`Converged`), so a universal failure would be attributed to the fixture | `logs/05` |
| C6 | **FAIL by construction** — identity passes at `0.00777 < 0.01`, test fails on the TD-003 `ΣY` bound `6.29e-07` vs `1e-8` | — | audit §6 |
| C7 | **PASS** — **343** passing, matching TD-004's baseline for the same exclusion | — | `logs/03` |

**Two criteria fail on the unchanged tree and that is the point:** C3 and C4d are the defects. C6
fails by construction. Everything else passes, so the gate is not vacuous and is not trivially
satisfiable.

**C3's non-vacuity is unusually strong and is worth stating plainly:** the unchanged baseline
contains both a passing level (the reference) and a failing level (window 16) for the *same*
criterion, so C3 is proved non-vacuous in both directions without constructing anything.

### 6.1 The collision this dry-run must resolve

Audit §9 records two readings of the bad mode, and they are not yet separated:

```text
(i)  an EXTRACTION fault -- the trial read its drift before the structure relaxed.
     Then C3 is TD-005's to fix, via relaxationTimesPerTrial.
(ii) TD-004's flashback surfacing under a different knob.
     Then C3 fails for an already-preserved reason and TD-005 STOPS at C3.
```

Sweep C is the discriminator: the bad mode appears at the shorter trial and the closer anchor, both
of which reduce relaxation, so **if (i) holds, raising `relaxationTimesPerTrial` removes it; if (ii)
holds, it does not.** This is written before the measurement so the gate is not shaped around the
answer, and so that a (ii) outcome is reported as TD-004's rather than absorbed into TD-005.

## 7. Freeze

```text
FROZEN 2026-10-05. The sha256 of this file and of every production, test and library artifact it
was frozen against is recorded in logs/00_freeze.log, which is written in the same action.
From that point no threshold, criterion or control in this file may be changed without a
separately authorized amendment carrying its own dry-run (CLAUDE.md 5.3).
```

**Corrections made BEFORE the freeze, all recorded in place rather than applied silently:**

```text
1. the 900 s cap is TD-004's HARNESS, not the repository's 7200 s        -> 4.4, 5.2
2. C4(a) needs no code change: the plateau test passes in 997 s          -> 5.2
3. C4d was ADDED -- the front position was unchecked by the draft        -> 4.7
4. a REJECTED admissibility criterion that both states satisfy           -> 4.3
5. the register's three controls are two, and one is TD-020-blocked      -> 4.5.1
6. audit 3's "~40x margin" was computed from the wrong knob              -> audit 3
```

## 8. Definition of done

1. C1–C7 all PASS, measured, with the numbers recorded;
2. the controls demonstrated, and the unexercisable one declared rather than claimed;
3. the regression expectations of §5.1 met, every delta enumerated, and §5.2's non-runner list
   unchanged except for the plateau test, which must have **moved from TIMEOUT to a verdict**;
4. `git diff --check` adds no warning beyond TD-018's three known files;
5. evidence written: `audit.md`, `GATE.md`, `reproducer.md`, `implementation.md`,
   `negative_controls.md`, `verification.md`, `regression.md`, `summary.md`;
6. `TECHNICAL_DEBT.md` and `TODO.md` updated **after** all of the above.
