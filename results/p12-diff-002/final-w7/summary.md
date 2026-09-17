# P12-DIFF-002 — Final authoritative W7 (post UC-001 + UF-001)

No previous W7 aggregate is reused. Evidence: `logs/01_W7.log`, `logs/01_ctest_raw.log`,
`logs/02_greengauss.log`, `logs/03_lowmach.log`, `logs/04_w8_tests.log`.

## 1. Tree state

```
git rev-parse HEAD   b66310ca871811c4c7671056beec291a451af55c
git diff --check     clean (no whitespace or conflict errors)
git status --short   294 modified, 83 untracked
git diff --stat      294 files changed, 93224 insertions(+), 80555 deletions(-)
```

HEAD predates P12-MESH-001…007, GRAD-001/002 and DIFF-001/002, so that diff is dominated by
**pre-existing uncommitted work from earlier phases**, not by this lineage.

**Production numerics vs the A4 freeze (`a4/production_freeze.txt`): 19 unchanged, 3 changed
exactly as the ThermalInterface fix recorded, 0 unexpected**, plus `src/solver/SolverRobustness.cpp`
at ROB-001's recorded `2b97f94a…`. All **11** DIFF-002-lineage acceptance gates verify intact, as do
the GRAD-001 (`2c45e438…`), GRAD-002 (`a46973ed…`, `353b72ef…`) and MESH-006/007 gate files.

## 2. Build provenance — the binaries demonstrably match the source

```
configure                        OK
clean-first rebuild of all       BUILD OK (433 lines, 119 s)
newest production source         src/solver/SolverRobustness.cpp  2026-09-16T16:00:47Z
test binaries                    41, of which older than that source: 0
libcfdcore.a before rebuild      143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a
libcfdcore.a after  rebuild      143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a
binaries lacking the x bit       0 (guard applied regardless)
```

The library is **bit-identical across a full clean rebuild** — a reproducible build, and proof the
recorded hash corresponds to these sources.

## 3. Result

```
1923 tests run
1919 passed
   4 failed
  45 disabled / not run   (1968 ctest entries)
Total test time 220.02 s
```

Against A6's fresh W7 (1923 / 1914 / **9**): the same 1923 run, **−5 failures**, exactly the 3 U-C
resolved by UC-001 and the 2 U-F resolved by UF-001. No test was added or removed.

## 4. Every failure classified — all four reproduce their recorded values exactly

| # | test | measured now | recorded | classification |
|---|---|---|---|---|
| 505 | `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | orders **1.93558 / 1.96913 / 1.98396** | 1.93558 / 1.96913 / 1.98396 | **known GRAD-002 failure** |
| 1827 | `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | velocity order 1.4391, dp/dx order **−5.012** | A1: dp/dx order −5.012 | **known W8 historical failure** |
| 1841 | `MultiBlockProductionCase.CurvedChannelGridConvergence` | G-error order **1.050** | A1: dp/dθ order 1.050 | **known W8 historical failure** |
| 1939 | `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | **1.0343290470e-04** vs 1e-4 | A6: 1.03433e-04 | **pre-existing unrelated failure** |

- **DIFF-002 regressions: 0.**
- **New or unexplained failures: 0.**
- **Resolved historical validation issues: 5** (3 U-C, 2 U-F), gone from the list.

`GridRefinementTest` fails *because the scheme became second order*: it asserts the distorted-mesh
observed order is **below 1.9** and the measured orders are 1.936–1.984. A2-7's explicit condition —
it "stays `PRE-EXISTING` only while it reproduces the recorded GRAD-002 values (1.936 / 1.969 /
1.984)" — is therefore **satisfied**, and checked rather than assumed.

## 5. Applying the frozen W7 semantics

Three wordings exist and they do not agree:

| source | wording | satisfied? |
|---|---|---|
| original `acceptance_gate.md` W7 | "Focused verification from P12-NUM, MESH-001…006 and **GRAD-002 passes**, each against its own originally frozen thresholds" | **NO** — the GRAD-002-era assertion does not pass |
| `acceptance_gate_A2.md` A2-7 | "no new unexplained regression. **A classification does not waive a frozen failure.**" | first half **YES**; second half **NO** |
| `acceptance_gate_A6.md` A6-j | "Fresh authoritative W7 … **0 unexplained DIFF-002-related failures**" | **YES** |

A6-j is a row in **A6's own** gate table, governing whether *A6* could proceed past its step j; it is
not a redefinition of W7 for DIFF-002 closure. A6's own verdict table still reads
"**W1-W10 all legitimately pass** → P12-DIFF-002 COMPLETE". The closure bar therefore remains the
original W7 wording, which is **not** met.

The two production-case failures are **not** counted against W7: the original gate carves them out
into **W8** ("…rerun unchanged"), and counting them at W7 would double-count them.
`LowMachRegressionTest` is a **P12-COMP** test and lies outside W7's named scope
(P12-NUM / MESH-001…006 / GRAD-002) — reported, not excluded.

**The single W7 blocker is `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`.**

## 6. Verdict

```
P12-DIFF-002 BLOCKED / FAILED W7
```

W8 was **not** run: the authorization permits it only once W7 legitimately passes.

## 7. The decision this surfaces

The blocker is circular and cannot be resolved inside any authorization granted so far:

- the test pins **pre-GRAD-002 first-order** distorted-mesh boundary behaviour (`order < 1.9`);
- GRAD-002 made that boundary treatment second order, so the assertion now fails **because the
  code improved** — the same obsolete-instrument class as the U-C and M-A tests already migrated;
- amending it is a **GRAD-002** decision, and every authorization in this lineage forbids modifying
  GRAD-002;
- but the plan's own ordering ("after DIFF-002 closure, return to GRAD-002") requires DIFF-002 to
  close first, which this test prevents.

Nothing here is a production defect: `libcfdcore.a` is bit-identical across a clean rebuild, the
production hashes match their recorded values, and 1919 of 1923 tests pass with zero new failures.
