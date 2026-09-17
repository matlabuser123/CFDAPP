# P12-DIFF-002 Amendment A6 — M-E and U-D numerical-policy reconciliation — acceptance gate

**This is the continuation of A6 after P12-DIFF-002-ROB-001.** The first A6 attempt stopped at its own
Step 1 with `A6 BLOCKED — PRODUCTION DEFECT FOUND`; that verdict was correct, is preserved verbatim in
`a6/summary.md` and `a6/logs/01–04`, and is **not** rewritten. ROB-001
(`rob-001/acceptance_gate.md` sha256 `1e2c98684b62b14418dd745ae7c5b8397b0547fb073212196c6fdc64753d3a2d`)
fixed the production defect A6 found. Resumed-A6 evidence lives in `a6/resumed/`.

Frozen **before any authoritative test is modified**. Production is frozen too and A6 changes none of
it: if a production change appears necessary, A6 stops and reports a new defect.

**Stop rule.** Stop at the first failed criterion. Never weaken a threshold, disable a test, delete an
assertion or reduce coverage to obtain a green result.

---

## 1. M-E — final classifications

| test | classification | evidence |
| --- | --- | --- |
| `SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline` | **KEEP** | `a6/resumed/logs/01` — passes unchanged on the ROB-001 library, file byte-identical `97fc7b61…` |
| `SIMPLERobustnessTest.DivergenceStatus` | **MIGRATE_POLICY** | `a6/resumed/logs/01`, `logs/02` |

The original A6 `PRODUCTION_DEFECT` classification of the first test **was correct** and was resolved
by ROB-001, not by amending the test. It is not migrated.

### A6-1 — `DivergenceStatus`, the only assertion amended

**Original criterion.** `EXPECT_EQ(result.iterations, 20u)` (`test_simple_robustness.cpp:304`).

**Original purpose.** The comment states the detector "stops it at the earliest possible iteration
(startIteration 10 + window 10 = 20)". The intent is promptness: the runaway must be caught as soon
as the detector is allowed to look.

**Observed failure.** 21, not 20. Every other assertion in the test passes: status `Diverging`,
fields finite, `final u / first u` = 1.664e+14 against its own 1e3 bound, and a `statusDetail` naming
divergence.

**Independent derivation** (`a6/resumed/tools/a6r_trigger_derivation.cpp`, read out of
`SolverRobustness.cpp`, not out of a measurement):

```text
earliest possible trigger  = max(startIteration, 1) + window
```

because the windowed rules are not evaluated at all before then (`SolverRobustness.cpp:481-482`).
With the defaults that is exactly **20** — so the historical value was the earliest-possible bound
asserted as if it were a prediction. It is a *lower* bound, not an equality.

```text
guaranteed trigger        <= max(startIteration, 1) + 2 * window
```

for a trajectory that actually runs away: with `b` the minimum over `[start, start + window)`, if
every value from `start + window` on exceeds `growth * b`, the whole window lies above the threshold
by `start + 2*window` and rule 1 fires. That is a property of the rule.

The one exception, stated explicitly: a residual norm that overflows to non-finite is reported
`Diverging` immediately, with no window (`SolverRobustness.cpp:471-480`). It cannot apply to this
test, which also asserts finite fields.

**Measured** (18 configurations, `a6/resumed/logs/02`): observed triggers 11–41, never below the
derived earliest and never above the derived guarantee; 20/20/21/20/20 across 4×4…16×16; and *which*
rule fires varies between rule 1 and rule 2. The trigger iteration is trajectory- and
settings-dependent, never a constant.

**New criterion.** Derived from the settings in the test, not from the observed output:

```text
status == Diverging
iterations >= max(divergence.startIteration, 1) + divergence.window        (earliest possible)
iterations <= max(divergence.startIteration, 1) + 2 * divergence.window    (derived guarantee)
+ every existing assertion unchanged: finite fields, growth >= 1e3, statusDetail names divergence
```

**Negative control** (`a6/resumed/logs/07`, run before this gate was frozen): the criterion rejects
all four degraded configurations and accepts the genuine one —

| control | result | rejected by |
| --- | --- | --- |
| P genuine runaway, detector on | ACCEPTED | — (as required) |
| D1 detector disabled | REJECTED | status, detail |
| D2 fires before mathematically possible (11) | REJECTED | **the lower bound** |
| D3 runaway recovered by adaptive relaxation | REJECTED | status, growth |
| D4 healthy converging run | REJECTED | status, growth |

**Why justified.** The amendment replaces an equality that the algorithm never promised with the two
bounds it does promise, and keeps every assertion that protects genuine divergence detection. D2
proves the lower bound has power, so promptness is still enforced.

## 2. U-D — the MMS upper-order bands

Four tests, eleven failing criteria (`a6/resumed/logs/03`), all of the form
"finest-**triplet** observed order in [lo, hi]":

| test | study | quantity/norm | band | triplet | reduction factors | finest **pairwise** |
| --- | --- | --- | --- | --- | --- | --- |
| `MomentumMMS.UConverges` | momentum central | u linf | [1.60, 2.40] | 2.647 | 5.184, 5.924, 4.671 | 2.223 |
| `MomentumMMS.VConverges` | momentum central | v linf | [1.60, 2.40] | 2.590 | 4.331, 5.772, 4.819 | 2.269 |
| `MomentumMMS.VConverges` | momentum linear_upwind | v linf | [1.60, 2.40] | 2.599 | 4.660, 5.521, 3.940 | 1.978 |
| `SIMPLEMMS.VelocityConvergesAtExpectedOrder` | simple | velocity u l2 | [1.60, 2.40] | 2.675 | 5.895, 4.281 | 2.098 |
| ″ | ″ | velocity u l1 | ″ | 2.604 | 5.658, 4.280 | 2.098 |
| ″ | ″ | velocity v l2 | ″ | 3.104 | 7.689, 4.503 | 2.171 |
| ″ | ″ | velocity v l1 | ″ | 3.138 | 7.806, 4.411 | 2.141 |
| `SIMPLEMMS.DistortedMesh` | distorted simple | velocity u l2 | [1.60, 2.40] | 2.921 | 7.105, 5.157 | 2.366 |
| ″ | ″ | velocity u l1 | ″ | 2.887 | 6.956, 5.139 | 2.361 |
| ″ | ″ | velocity v l2 | ″ | 3.101 | 8.007, 5.459 | 2.448 |
| ″ | ″ | velocity v l1 | ″ | 3.070 | 7.827, 5.340 | 2.417 |

Every one is labelled `monotonic_not_asymptotic` by the framework itself — the errors decrease
monotonically at every refinement, and the framework knows the triplet is not in its asymptotic
range, yet still gates on the triplet value.

### A6-2 — cause, established rather than assumed

Step 2's checklist, answered from measurement:

| candidate cause | verdict |
| --- | --- |
| legitimate higher accuracy | **yes, in part** — the boundary ring is genuinely ~3rd order |
| polynomial exactness | **no** — the manufactured field is `psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y)/pi`, transcendental; DIFF-002's quadratic reconstruction cannot be exact for it |
| non-asymptotic meshes | **yes — the primary cause** |
| solver tolerance floor | **no** — mass imbalance 1e-33…1e-20, errors 1e-5…1e-3 |
| boundary/interior interaction | **yes** — two different rates in one norm |
| broken measurement | **yes, contributing** — a three-level Richardson estimator on a non-asymptotic sequence |
| production defect | **no evidence** |

**Independent derivation (Step 3).** DIFF-002 adds a boundary-ring error of order 3 to an interior
error of order 2, so the total behaves as

```text
E(h) = A h^2 + B h^3
```

whose pairwise observed order `p(h) = log_r(E(rh)/E(h))` lies strictly between **2 and 3**, decaying
to 2 as h → 0. The three-level Richardson estimator is a difference of differences and **overshoots
beyond 3** during that decay — which is exactly the 2.6–3.1 the tests report.

**Verified by extending the refinement range beyond what the tests use**
(`a6/resumed/logs/05`, 16²…256², momentum central):

```text
u   l2    pairwise 2.382, 2.229, 2.083, 2.017      triplet 2.421, 2.271, 2.105
u   linf  pairwise 2.374, 2.567, 2.224, 1.988      triplet 2.332, 2.647, 2.295
v   linf  pairwise 2.115, 2.529, 2.269, 1.978      triplet 2.010, 2.590, 2.356
u_boundary_ring l2  pairwise 3.017, 2.932, 2.948, 2.947   <- asymptotic at ~2.95, not a transient
```

So every failing quantity settles at **the formal order 2** (1.99–2.04) once the grid is fine enough,
while the boundary ring is asymptotically **~2.95**. The gated triplet value 2.647 is a local
overshoot in the 32/64/128 window; the next triplet is 2.295 and the finest pairwise is 1.988.

**Pre-DIFF-002 comparison** (`a6/resumed/logs/04`, `logs/06`): all 19 MMS tests **pass**, with
momentum-central u linf 2.148/1.904 labelled `asymptotic`, and a boundary ring of order
**1.789, 1.886, 1.949, 1.976** — i.e. 2. The bands and the estimator were calibrated on a
**single-rate** error. Accuracy at 256²: v l1 1.13e-05 → 6.56e-06 (1.7× better), u linf 2.29e-05 →
1.40e-05 (1.6× better), boundary ring 1.03e-05 → 3.55e-07 (**29× better**); u l1/l2 are ~3 % worse
(5.65e-06 → 5.84e-06) — recorded, not hidden, and immaterial at the same formal order.

**Conclusion: validation-policy defect, not a production defect.** The formal order is preserved and
the boundary treatment is measurably better; what fails is a fixed upper band on an estimator the
framework itself flags as pre-asymptotic.

### A6-3 — the U-D amendment

**Original criterion.** `addOrderGate(...)` gates the **finest-triplet** observed order into
[lo, hi], hi = 2.40.

**Original purpose.** Detect loss of formal accuracy, and detect an implausibly high order that would
signal accidental exactness or broken instrumentation.

**New criterion**, applied uniformly to the **velocity** order gates of the four affected studies —
not cherry-picked to the failing norms:

1. gate on the **finest pairwise** observed order, whose theoretical range for a two-rate
   `A h^2 + B h^3` error is exactly (2, 3), instead of the triplet, which has no such bound;
2. upper limit **3.00** — the faster component's own order, measured at 2.93–3.02, not an arbitrary
   widening;
3. every existing lower bound unchanged (that is what detects loss of accuracy);
4. the monotone-decrease gates unchanged;
5. **plus a new gate**, strengthening the suite: the boundary-ring observed order must lie in
   **[2.50, 3.50]**, which pins DIFF-002's second-order wall flux directly.

**Negative control** (already executed, `a6/resumed/logs/06`): the new boundary-ring gate **fails on
the pre-DIFF-002 library** (order 1.976 < 2.50) and passes now (2.947) — i.e. it detects a *disabled
DIFF-002 reconstruction*, one of Step 4's required detections. The order band alone does not detect
that (historical finest pairwise 1.963–1.976 sits inside [1.80, 3.00]), which is precisely why the
boundary-ring gate is added rather than the band merely widened.

**Why justified.** No upper bound is removed: one is replaced by the mathematically derived limit of
the faster error component, measured on this very discretization, and the estimator is changed to the
one whose range that derivation bounds. Coverage increases.

## 3. Criteria

| id | criterion | threshold |
| --- | --- | --- |
| **A6-a** | Production byte-identical to `a6/resumed/production_freeze_A6.txt` throughout A6 | by hash; any change ⇒ `A6 BLOCKED — PRODUCTION DEFECT FOUND` |
| **A6-b** | Both M-E tests classified with evidence; `DefaultRobustnessPreservesBaseline` **KEEP** and unmodified | file byte-identical |
| **A6-c** | `DivergenceStatus` amended only in its timing assertion, per §1; all other assertions untouched | reviewed diff |
| **A6-d** | Non-vacuity: the amended `DivergenceStatus` criterion rejects D1–D4 | 0 wrong verdicts |
| **A6-e** | U-D amended per §2.3 only; lower bounds and decrease gates unchanged; the new boundary-ring gate added | reviewed diff |
| **A6-f** | Non-vacuity: the boundary-ring gate fails on the pre-DIFF-002 library and passes now | measured |
| **A6-g** | The whole MMS suite passes after the amendment | 19/19 |
| **A6-h** | `CFDSimpleTests` and `CFDSolverTests` pass; ROB-001's three sequences still pass | exact counts |
| **A6-i** | U-C / U-F / U-H audited individually with quantitative evidence; no `PRODUCTION_DEFECT` | any ⇒ STOP |
| **A6-j** | Fresh authoritative W7 with verified library and binary hashes | 0 unexplained DIFF-002-related failures |
| **A6-k** | W8 run unchanged at its own decision point, raw errors recorded before any order is computed | not amended in A6 |
| **A6-l** | Untouched: the ThermalInterface fix, the A5 KEEP invariant, ROB-001, W8, GRAD-002, MESH-007, the MESH-004 ASan defect | by hash |

## 4. Verdicts

```text
a production change proves necessary        -> P12-DIFF-002 A6 BLOCKED — PRODUCTION DEFECT FOUND
a classification cannot be established      -> P12-DIFF-002 A6 BLOCKED — POLICY CLASSIFICATION UNCERTAIN
any A6 criterion fails                      -> P12-DIFF-002 BLOCKED / FAILED A6 GATE
W8 fails at its decision point              -> P12-DIFF-002 BLOCKED — HISTORICAL W8 GATE DECISION REQUIRED
W1-W10 all legitimately pass                -> P12-DIFF-002 COMPLETE — READY FOR GRAD-002 REVALIDATION
```

No commit. No push. No other P12 phase.
