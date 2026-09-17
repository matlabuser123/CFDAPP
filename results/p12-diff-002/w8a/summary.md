# P12-DIFF-002 W8 Amendment — STOPPED before execution

Gate: [acceptance_gate.md](acceptance_gate.md), frozen at `2707f72b…` in
[logs/00_freeze.log](logs/00_freeze.log) before any test edit.

## Chronology — recorded, not rewritten

```
Original W8:    FAILED unchanged historical gate                       (w8/, preserved byte-identical)
W8-INV-001:     COMPLETE                                               (w8-inv-001/, preserved byte-identical)
W8 Amendment:   authorized
Amended W8:     NOT EXECUTED — STOPPED: two §13 replacement criteria cannot be reproduced exactly
```

The original W8 is **not** marked as passed. No amended W8 was built or run, so there is no PASS or
FAIL for it. No non-vacuity control was executed. W9 and W10 were **not** started.

## Stop condition triggered

The authorization says: *"If any replacement criterion cannot be reproduced exactly from §13,
STOP."* It also says *"Use exactly the derivation and numerical envelope recorded in §13"* and
describes the G envelope as *"independently justified"*. Two of the four §13 criteria fail that
test against the investigation's own frozen data.

### Finding 1 — W8-R2 velocity envelope (W8A-1): §13's wording and its named reference disagree

§13: *"velocity L2 at each grid **≤ 1.0 × the Cartesian scheme's own L2 at the same `ny`**"*, derived
as *"MESH-001 already computes `cartesianVelocityL2(ny)`"*, value quoted `1.5183e-02`.

`cartesianVelocityL2` (`test_structured_quad_production_case.cpp:209-221`) computes the
**superseded two-point** Cartesian solution: the `n2/(n2+2)` gradient factor and the `+g·dy²/8`
offset. UC-001's frozen gate (§1, `bed6b8941a7e0112…`) lists both as superseded, and the migrated
helper `discretePoiseuilleVelocity` (`PoiseuilleValidationUtils.cpp:185-193`) now uses the DIFF-002
form: the exact parabola at cell centres, factor `2n²/(2n²+1)`, no offset. Under DIFF-002 the
helper is therefore **not** "the Cartesian scheme's own L2".

Same RMS definition as the helper (`tools/cartesian_reference.awk`; the helper column reproduces the
original W8 log to every printed digit):

| ny | distorted, gate (W8 log) | helper = two-point | ratio | **DIFF-002 Cartesian** | ratio |
|---|---|---|---|---|---|
| 8 | 1.0827e-02 | 1.5183e-02 | 0.713 ✓ | 8.4927e-03 | **1.275 ✗** |
| 12 | 4.7562e-03 | 6.9492e-03 | 0.684 ✓ | 3.7905e-03 | **1.255 ✗** |
| 18 | 2.6537e-03 | 3.1294e-03 | 0.848 ✓ | 1.6879e-03 | **1.572 ✗** |

The two readings give opposite verdicts on current production, and neither is sound:

- **Read by its wording** (the current Cartesian scheme), W8A-1 rejects current production on every
  grid.
- **Read by the named helper**, the criterion compares against a stale reference, the same defect
  UC-001 removed. Its verdict at 144×18 also depends on where the iteration stops. At the
  20000-iteration plateau (`w8-inv-001/logs/06_plateau.log`) the distorted L2 is 3.7690e-03 =
  **1.204× the helper value ✗**, while at the gate it is 0.848× ✓.

### Finding 2 — W8-R3 G envelope (W8A-4): the stated justification cannot be reproduced; the verdict depends on where the iteration stops

§13: *"`|G − G_exact| / |G_exact| ≤ 0.30 %` at the finest grid … it sits **above** the iterative
uncertainty (≈ 0.17 % of `G_exact`) and **below** the baseline's error."*

From the raw plateau logs (`w8-inv-001/logs/08`, `11`), 18×45, `G_exact = −1.8282207204`:

| library | G error, gate | rel. | G error, 20000 it | rel. | W8A-4 at gate / plateau |
|---|---|---|---|---|---|
| current DIFF-002 | +2.81652846e-03 | 0.154 % | +6.76807422e-03 | **0.370 %** | ✓ / **✗** |
| two-point baseline | +1.26194628e-02 | 0.690 % | +1.57469119e-02 | 0.861 % | ✗ / ✗ |

- The measured iterative drift at 18×45 is 3.9515e-03 = **0.216 %** of `|G_exact|`. The ≈ 0.17 %
  in §13 cannot be reproduced from any logged number. Nor can "≈ 1e-2 absolute": the logged drifts
  are 4.18e-02, 1.97e-02 and 3.95e-03.
- The 0.30 % threshold falls **inside** the current solution's own iterative band
  [0.154 %, 0.370 %]. Whether it passes is therefore set by the stopping point, which is the defect
  class the amendment was authorized to remove.
- §13's own R4 classifies G as not iteratively converged (140–507 %). An accuracy envelope on that
  quantity inherits the same contamination.
- 0.30 % sits between the current gate value (0.154 %) and the baseline (0.690 %). No calculation in
  §13 derives it from the velocity error, so it is consistent with a threshold fitted to current
  output, which §13's own design rule forbids.

### The criteria that do reproduce

| criterion | reference | current gate | current plateau | two-point gate / plateau |
|---|---|---|---|---|
| W8A-2 dp/dx at 144×18 ≤ 2/649 = **0.308 %** | UC-001 `1/(2ny²+1)`, DIFF-002-correct | 0.207 % ✓ | 0.298 % ✓ (3 % margin) | 0.679 % / 0.774 % ✗ |
| W8A-3 MultiBlock velocity order ≥ 1.5 (kept) | formal order 2 | 2.0538 / 2.0567 ✓ | 2.0402 / 2.0609 ✓ | 1.9190 / 1.9752 (passes; order is not a discriminator) |
| W8A-5 (R4) velocity drift ≤ 10 % of the error | §13 R4 | — | 0.10 / 0.45 / **0.28 %** ✓ | — |
| W8-R1 non-order assertions unchanged | — | nothing to implement | — | — |

### W8A-5 iteration-budget calibration (`w8-inv-001/logs/12_r4_calibration.log`)

The budget rule was pre-registered in gate §5 before this log was read. Grid 18×45, current library
`143a1dda…`:

| budget | velocity L2 | drift from gate (3160 it) | % of error | share of the 20000-it drift |
|---|---|---|---|---|
| 5000 | 2.0959589009e-03 | 8.2012e-07 | 0.039 % | 14 % |
| 10000 | 2.0983948683e-03 | 3.2561e-06 | 0.155 % | 56 % |
| 20000 | 2.1009843155e-03 | 5.8455e-06 | **0.279 %** | 100 % |

- **B = 20000.** Neither smaller budget reproduces the 20000-iteration drift to within 20 %.
- The 20000 value reproduces log 08 (`2.10098432e-03`), so the probe is deterministic.
- Implementing W8A-5 as specified therefore adds one 20000-iteration solve, about **430 s** (log 08:
  433.6 s), to `CurvedChannelGridConvergence`.
- *Observation:* the drift grows roughly in proportion to the iteration count rather than
  saturating. "20000 iterations" is a fixed-budget comparison, as §13 defines it, not a converged
  reference. It is still about 36× below R4's 10 % bound.

## My own error, recorded

I wrote §13 during W8-INV-001. It contains two derivation defects, and both would have been caught
by applying §13's **own** R4 test to the replacement envelopes: an envelope must give the same
verdict at `q(gate)` and at `q(20000 iterations)`.

1. It named an existing helper as "the Cartesian scheme's own L2" without checking the helper
   against the post-DIFF-002 Cartesian solution.
2. It set the G envelope without checking it against the plateau data (log 08) that the same
   section used to disqualify G's order.

§13's non-vacuity table also stated far-cell ×2 rejection on MultiBlock, which was never measured.
The gate disclosed this (§6) before anything was run. `w8-inv-001/summary.md` is frozen
(`a795cc4b…`) and **was not edited**; this section is the erratum.

## What was done and what was not

| item | state |
|---|---|
| gate | frozen `2707f72b…`. Attempt 1 is preserved as `logs/00_freeze_attempt1_wrong_paths.log`: four guessed production paths did not exist, and the gate text misattributed the two thermal hashes. Both were corrected before any edit, and before the calibration was read. |
| StructuredQuad test | W8A-1 and W8A-2 were **edited in, then reverted** when Finding 1 was established. The attempt is preserved as `data/test_structured_quad_production_case.cpp.attempted` (`36b44f26…`) and `data/attempted_structured_quad.patch`. The source is verified back at `d20c8443…`. |
| MultiBlock test | **never edited** (`bcbbadb7…`) |
| build / amended W8 run | **not performed**. `tools/run_w8a.sh` was written and never executed. |
| non-vacuity controls | **not executed**. The three control trees were inspected only (`logs/04`): their W8 test sources, both case directories and the test helpers are byte-identical to the repo's pre-amendment state. |
| R4 budget calibration | additive `r4cal` mode in `w8-inv-001/tools/w8inv_multiblock.cpp` (a closed investigation's tool; existing modes unchanged); log `w8-inv-001/logs/12_r4_calibration.log` |
| housekeeping | four W8-INV-001 waiter loops terminated (PIDs 92963, 96544, 98237, 93179). They never exited because `pgrep -f` matched their own command lines; no data was involved. |
| W8 report JSONs | unchanged; snapshot in `data/reports_before/` |
| production | **8/8** at frozen hashes (`logs/05`) |
| MESH-001 / MESH-003 | summary, gate and summary byte-identical |
| frozen gates | 13 lineage gates plus 4 earlier P12 gates, all unchanged |
| W9, W10 | **not started** |
| GRAD-002 iterative-drift question | **open, separate, not investigated** |
| commit / push | none |

## Observation, not acted on

MESH-001's frozen per-grid bound `velocity L2 ≤ 1.5 × cartesianVelocityL2(ny)`, inside
`expectPoiseuilleGates`, uses the same stale helper. It passes in both iterative states (maximum
1.204× at the 144×18 plateau), so it is not failing. Its reference is still pre-DIFF-002. The gate
was not modified.

The same applies to the StructuredQuad test's retained GCI bracket: it passes only because the
framework's non-asymptotic triplet order (p = 0.276) inflates GCI21 to 1.9 %.

## Decision required (not self-authorized)

1. **Re-derive W8A-1 and W8A-4 under an added rule.** Every replacement envelope must give the same
   verdict at the gate and at the 20000-iteration plateau, for current production and for every
   control, and must use a DIFF-002-consistent reference. W8A-2, W8A-3 and W8A-5 would carry over
   unchanged. For the StructuredQuad velocity, no fixed multiple of the DIFF-002 Cartesian L2
   separates current from baseline in both states at 144×18. Current is 1.572× at the gate and
   2.233× at the plateau; the baseline is 2.197× and 2.725×. So a velocity envelope may not be
   achievable on this unconverged family.
2. **Or any other disposition you choose** for the StructuredQuad velocity order and the
   MultiBlock G order.

Until then, P12-DIFF-002 stays **BLOCKED at W8**.
