# P12-DIFF-002 Amendment A6, resumed after ROB-001 — report

**Gate:** `results/p12-diff-002/validation-migration/acceptance_gate_A6.md`, sha256
`ba33ced0073fb0e1026fda63771a0a07ea73620a79e2a140bfd5525f33d3393d`, frozen before any authoritative
test was modified (`logs/08_A6_gate_freeze.log`). Production freeze
`production_freeze_A6.txt` (`95abf425…`).

The **first** A6 attempt's verdict, `A6 BLOCKED — PRODUCTION DEFECT FOUND`, is preserved verbatim in
`a6/summary.md` and `a6/logs/01–04` and is **not** rewritten. It was correct: ROB-001 fixed the defect
it found. This report covers only the resumed work, in `a6/resumed/`.

```text
Step 1  M-E re-evaluation        KEEP + MIGRATE_POLICY
Step 2  U-D investigation        11 failing criteria characterised
Step 3  theoretical derivation   two-rate error; verified at 256^2
Step 4  non-vacuity              4/4 divergence controls; MMS control executed
Step 5  A6 gate frozen           ba33ced0…
Step 6  DivergenceStatus         migrated -> SIMPLERobustnessTest 10/10
Step 7  U-D migrated             MMS suite 19/19
Step 8  U-C/U-F/U-H audited      no PRODUCTION_DEFECT
Step 9  fresh authoritative W7   see section 7
```

---

## 1. Step 1 — M-E final classifications

| test | classification | evidence |
| --- | --- | --- |
| `DefaultRobustnessPreservesBaseline` | **KEEP** | passes unchanged on the ROB-001 library; file byte-identical `97fc7b61…` (`logs/01`) |
| `DivergenceStatus` | **MIGRATE_POLICY** | `logs/01`, `logs/02` |

The first A6 attempt's `PRODUCTION_DEFECT` classification of `DefaultRobustnessPreservesBaseline`
**was correct and was resolved by ROB-001**, not by amending the test. It is not migrated.

## 2. Step 1B — what the detector actually guarantees

`tools/a6r_trigger_derivation.cpp`, `logs/02`. Read out of `SolverRobustness.cpp`, not out of a
measurement:

| quantity | value |
| --- | --- |
| **earliest possible** | `max(startIteration, 1) + window` = **20** for the defaults — the windowed rules are not evaluated before then (`:481-482`) |
| **guaranteed** | `max(startIteration, 1) + 2*window` = 30 — once every value past `start + window` exceeds `growth ×` the early-window minimum, the whole window is above threshold and rule 1 fires |
| **observed** | 20/20/21/20/20 on 4×4…16×16; **11–41** across 18 configurations; rule 1 and rule 2 alternate |
| **historical** | 20 — i.e. the earliest-possible value, asserted as if it were a prediction |

Exception recorded: a residual norm overflowing to non-finite is reported `Diverging` with no window
(`:471-480`); it cannot apply to a test that asserts finite fields.

Both derived bounds held in **all 18** configurations, with 0 runaways missed.

## 3. Steps 2–3 — the U-D failures

Eleven failing criteria across four tests, all of the form "finest-**triplet** observed order in
[lo, hi]", every one labelled `monotonic_not_asymptotic` by the framework itself. Raw data in
`logs/03`; the full table is §2 of the gate.

**Cause, established:**

| candidate | verdict |
| --- | --- |
| legitimate higher accuracy | **yes, in part** — the boundary ring is genuinely ~3rd order |
| polynomial exactness | **no** — the field `psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y)/pi` is transcendental |
| non-asymptotic meshes | **yes, the primary cause** |
| solver tolerance floor | **no** — mass imbalance 1e-33…1e-20 against errors 1e-5…1e-3 |
| boundary/interior interaction | **yes** — two rates inside one norm |
| broken measurement | **yes, contributing** — Richardson triplet on a non-asymptotic sequence |
| production defect | **no evidence** |

**Independent derivation.** DIFF-002 adds a boundary-ring error of order 3 to an interior error of
order 2, so `E(h) = A h² + B h³`, whose pairwise observed order lies strictly in (2, 3) and decays to
2; the three-level Richardson estimator, being a difference of differences, **overshoots past 3**
during that decay — which is exactly the 2.6–3.1 reported.

**Verified by refining beyond the tests' range** (`logs/05`, 16²…256², momentum central):

```text
u   l2            pairwise 2.382, 2.229, 2.083, 2.017
u   linf          pairwise 2.374, 2.567, 2.224, 1.988      (gated triplet 2.647 -> next triplet 2.295)
v   linf          pairwise 2.115, 2.529, 2.269, 1.978
u_boundary_ring l2 pairwise 3.017, 2.932, 2.948, 2.947     <- asymptotic at ~2.95, not a transient
```

Every failing quantity settles at **the formal order 2** (1.99–2.04) once the grid is fine enough.

**Pre-DIFF-002 comparison** (`logs/04`, `logs/06`): all 19 MMS tests pass, momentum-central u linf
2.148/1.904 labelled `asymptotic`, boundary ring 1.789, 1.886, 1.949, **1.976** — order 2. The bands
were calibrated on a single-rate error. Accuracy at 256²: v l1 1.7× better, u linf 1.6× better,
boundary ring **29× better** (1.03e-05 → 3.55e-07); u l1/l2 ~3 % worse (5.65e-06 → 5.84e-06) —
recorded, not hidden, and immaterial at the same formal order.

**Conclusion: validation-policy defect, not a production defect.**

## 4. Step 4 — non-vacuity, before the gate was frozen

**DivergenceStatus criterion** (`logs/07`): the genuine runaway is ACCEPTED and all four degraded
configurations are REJECTED —

| control | verdict | rejected by |
| --- | --- | --- |
| P genuine runaway, detector on | ACCEPTED | — |
| D1 detector disabled | REJECTED | status, detail |
| D2 fires at 11, before mathematically possible | REJECTED | **the lower bound** |
| D3 runaway recovered by adaptive relaxation | REJECTED | status, growth |
| D4 healthy converging run | REJECTED | status, growth |

**MMS criterion** (`logs/10`, executed after the migration): on the pre-DIFF-002 library the amended
**order bands still pass** (u l2 1.981, v l2 1.976, inside [1.80, 3.00]) — they have no power to
detect a disabled DIFF-002 — while the **new boundary-ring gate fails** (1.949 and 1.937 < 2.50),
failing `MomentumMMS.UConverges`. That is precisely why the gate was added rather than the band
merely widened.

## 5. Steps 6–7 — what was amended

**`DivergenceStatus`** (`tests/solver/simple/test_simple_robustness.cpp`): the single assertion
`EXPECT_EQ(result.iterations, 20u)` replaced by the two derived bounds, computed from
`settings.robustness.divergence` rather than from an observation, with `EXPECT_EQ(earliestPossible,
20u)` keeping the historical number visible as the bound it is. Every other assertion untouched.
Result: `SIMPLERobustnessTest` **10/10 pass**.

**U-D** (`MMSCases.{hpp,cpp}`, `test_mms_momentum.cpp`, `test_mms_simple.cpp`):

* new helper `addPairwiseOrderGate`, gating the finest **pairwise** observed order;
* the velocity order gates of the four affected studies switched to it — uniformly, not cherry-picked
  to the failing norms — with upper limit `formal + 1`, the faster component's own order;
* every lower bound and every decrease gate unchanged;
* **added**: a boundary-ring order gate `[2.50, 3.50]` for the second-order schemes, pinning
  DIFF-002's wall flux. Skipped for first-order upwind, where the interior error swamps the wall term
  (the quantity is still recorded there).

Scope note: the SIMPLE studies record only `p_boundary_ring`, not a velocity boundary ring, so the
new gate lands in the momentum studies, where the 16²…256² measurement exists. Result: MMS suite
**19/19 pass**.

## 6. Step 8 — U-C / U-F / U-H audit

Every failure measured individually (`logs/11`, `logs/12`). **No `PRODUCTION_DEFECT`.**

| test | class | quantitative evidence |
| --- | --- | --- |
| `PoiseuilleValidation.Profile` | **VALIDATION_DEFECT** | asserts centerline == 1.45454545, the *superseded* scheme's own closed form (16/11). Production gives 1.46511967; distance to the exact 1.5 is **0.0349 vs 0.0455 — 1.30× better** |
| `PoiseuilleValidation.PressureDrop` | **VALIDATION_DEFECT** | asserts the dp/dx error *equals* 2/66 = 3.03 %, the superseded scheme's error. Production's is **0.86 % — 3.5× better** (−1.189705 vs exact −1.200000; the old reference is −1.163636) |
| `PoiseuilleValidation.ProductionGridConvergence` | **VALIDATION_DEFECT** | same discrete-exact reference, same mechanism |
| `NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3` | **EXPECTED_DIFFT002_CHANGE** | v_max error 0.13026 vs bound 0.12 (1.086×). INV-002's 2×2 factorial attributes it to the **momentum wall shear alone**; the thermal reconstruction improves Nusselt and leaves the extrema untouched. The reference is independent literature and must not be redefined |
| `…Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3` | **EXPECTED_DIFFT002_CHANGE** | identical value, 0.13026 vs 0.12 |
| `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | **PRE_EXISTING** | 1.03433e-04 vs 1e-4 (1.034×). INV-002: the metric **grows** under refinement in the pre-DIFF-002 build too and already exceeds 1e-4 there at 64×12; 10³× tighter solver tolerances do not reduce it |
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | **PRE_EXISTING** | asserts the distorted-mesh order is *below* 1.9 ("suspiciously high"); measures 1.936/1.969/1.984. A GRAD-002-era test pinning the old first-order boundary behaviour |
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | **PRE_EXISTING** (W8) | not amended; Step 10 territory |
| `MultiBlockProductionCase.CurvedChannelGridConvergence` | **PRE_EXISTING** (W8) | not amended; Step 10 territory |

**None of these is resolvable inside A6's mandate.** U-C needs a genuinely new discrete-exact
reference for the three-point wall flux — a numerical derivation, and Step 7 authorizes amending only
U-D. U-F needs a decision on an independent literature benchmark's bound, which the authorization
explicitly forbids redefining. U-H is W8/GRAD-002 history, preserved by instruction.

## 7. Step 9 — fresh authoritative W7

`logs/13_W7_fresh.log` (configure → build everything → verify library and binary hashes → execute; no
A1/A2 count reused). Library `143a1dda0680bc1d…`, every test binary freshly built and hashed.

```text
1923 tests run, 1914 passed, 9 failed, 90 disabled     (197.72 s)
```

For comparison, A4's clean-build inventory was **1913 run, 1877 passed, 36 failed**. The accounting
is exact:

```text
tests:     1913 + 7 (ConjugateBoundaryEquivalence) + 3 (ROB-001 ResidualTrend) = 1923
failures:  36 - 17 (A5 M-A) - 1 (A5 M-B species) - 1 RegionAwareThermalDiffusion
              - 1 ConjugateConductionPathIsConsistentToo - 1 ConvergedFieldIsConsistent
              - 1 DefaultRobustnessPreservesBaseline (ROB-001)
              - 4 (A6 U-D MMS) - 1 DivergenceStatus (A6)                      =  9
```

The 9 remaining failures are **exactly** the 9 tests audited in §6 — every one explained, classified
and evidenced, none unexplained, and none newly failing:

```text
505  GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment  U-H PRE_EXISTING
1759 PoiseuilleValidation.Profile                                                        U-C VALIDATION_DEFECT
1761 PoiseuilleValidation.PressureDrop                                                   U-C VALIDATION_DEFECT
1762 PoiseuilleValidation.ProductionGridConvergence                                      U-C VALIDATION_DEFECT
1782 NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3                        U-F EXPECTED_DIFFT002_CHANGE
1783 NaturalConvectionValidation.Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3    U-F EXPECTED_DIFFT002_CHANGE
1827 StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence                     U-H PRE_EXISTING (W8)
1841 MultiBlockProductionCase.CurvedChannelGridConvergence                               U-H PRE_EXISTING (W8)
1939 LowMachRegressionTest.GlobalMassImbalanceIsSmall                                    U-F PRE_EXISTING
```

**W7 does not pass, and A6 stops here.** Step 9's precondition — "only after all DIFF-002-related
validation issues are resolved" — is not met: five of the nine (the three U-C plus the two
natural-convection U-F) are DIFF-002-related and **unresolved**. They are explained, not fixed, and
neither can be fixed under A6's mandate:

* **U-C** needs a genuinely new discrete-exact reference for the three-point wall flux. That is a
  numerical derivation, and Step 7 authorizes amending **only** U-D. Writing the current output into
  the test instead would be exactly the prohibited "adopt production output as the expected value" —
  and would also discard a real accuracy gain (dp/dx error 3.03 % → 0.86 %).
* **U-F** needs a decision on the bound of an independent literature benchmark (de Vahl Davis), which
  this authorization explicitly forbids redefining.

Since W7 does not pass, **Step 10's W8 decision point was not reached** and W8 was not run as a gate;
its two tests remain failing and unamended, exactly as frozen. W9/W10 were not reached. DIFF-002 is
therefore **not** marked complete.

### Re-verification after clang-format (`logs/15`)

`MMSCases.{hpp,cpp}` were reformatted after the W7 run, so both affected suites were rebuilt and
re-run: MMS **19/19**, `SIMPLERobustnessTest` **10/10**. `test_simple_robustness.cpp`,
`test_mms_momentum.cpp` and `test_mms_simple.cpp` were already format-clean.

## 8. Files modified

```text
tests/solver/simple/test_simple_robustness.cpp   A6-1, the timing assertion only
tests/integration/mms/MMSCases.hpp               + addPairwiseOrderGate declaration
tests/integration/mms/MMSCases.cpp               + addPairwiseOrderGate implementation
tests/integration/mms/test_mms_momentum.cpp      A6-3: pairwise gates + boundary-ring gate
tests/integration/mms/test_mms_simple.cpp        A6-3: pairwise gates
results/p12-diff-002/validation-migration/acceptance_gate_A6.md   the frozen gate
results/p12-diff-002/validation-migration/a6/resumed/             this report, logs 01-13, 3 probes
TODO.md                                          chronology
```

**A6 changed no production file.** The first A6 attempt's evidence, ROB-001, the A5 KEEP invariant,
W8, GRAD-002, MESH-007 and the MESH-004 ASan defect are untouched.
