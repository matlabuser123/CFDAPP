# P12-DIFF-002 W8 AMENDMENT B (W8B) — acceptance gate

Authorized 2026-09-17: resolve the DIFF-002 → GRAD-002 → MESH-007 chain, and re-derive W8A-1 and
W8A-4 from the strongest independent references. Frozen **before** any repository test or case
file is edited; hashes are in [logs/00_freeze.log](logs/00_freeze.log). The candidate test files are
frozen byte for byte in [data/candidate/](data/candidate/).

## 0. Chronology — recorded, not rewritten

```text
Original W8     FAILED   (w8/, preserved byte-identical)
W8-INV-001      investigation; misread the linear-flux drift as a "residual plateau" (erratum below)
W8A             BLOCKED  (w8a/: two §13 envelopes not reproducible; preserved byte-identical)
DRIFT-001       root cause: undamped odd-even pressure mode of the 2D linear flux
                (results/p12-grad-002/drift-001/)
W8B             this gate: DRIFT-001's case fix + independently derived replacements
W8B run         <pending>
```

**Erratum to W8-INV-001 (frozen, not edited).**

- Its "plateau" is not a plateau: the linear-flux solution keeps moving past 40 000 iterations.
- Its classification of the StructuredQuad family as "not self-similar, cannot support an order" is
  **refined**, not overturned. Once the solution is stationary:
  - the velocity order is asymptotic: 2.11 → 2.07 → 2.03 at r = 1.5, and 2.09 → 2.03 at r = 2;
  - the family *is* non-asymptotic for dp/dx, for the reason given in §3 (a zero crossing), not
    because of the mapping.

## 1. Prerequisite

The DRIFT-001 fix gate ([results/p12-grad-002/drift-001/acceptance_gate.md](../../p12-grad-002/drift-001/acceptance_gate.md))
is frozen in the same freeze log, and its F1 is satisfied: the two W8 cases select
`"face_flux": "rhie_chow"`.

## 2. W8B-1 — migrate MESH-001's per-grid references (obsolete instrument)

`cartesianVelocityL2(ny)` and `cartesianPressureGradientError(ny)` in
`test_structured_quad_production_case.cpp` encode the **superseded two-point** Cartesian solution:
the factor `ny²/(ny²+2)`, the `+G dy²/8` offset, and `2/(ny²+2)`. This is the same obsolete-constant
class UC-001 migrated in the Poiseuille validation tests.

- **Replacement:** UC-001's independently derived DIFF-002 discrete-exact solution. It was solved
  in exact rational arithmetic with no CFDApp code
  (`results/p12-diff-002/uc-001/acceptance_gate.md` §1, frozen `bed6b894…`):
  - `u_j = (G/2) y_j (H − y_j)`, `G = 12 μU/H² · 2ny²/(2ny²+1)`;
  - relative dp/dx error `1/(2ny²+1)`.
- **Independent confirmation** (DRIFT-001 §2): the production solver on an *orthogonal*
  `structured_quad` mesh reproduces both formulas to six digits at ny = 8, 12, 16, 18, 27, 32.
- **The factor 1.5 is unchanged.** It is MESH-001's pre-registered design requirement, "the
  non-orthogonal mesh may cost at most 50 % more error than a Cartesian mesh of the same
  resolution", fixed before DIFF-002 existed. It is not fitted to any DIFF-002 output.
- **Uncertainty check** (no threshold tighter than the unresolved uncertainty):

  | grid | velocity bound | measured | dp/dx bound | measured |
  |---|---|---|---|---|
  | 64×8 | 1.2739e-2 | 1.0921e-2 | 1.1628 % | 0.3113 % |
  | 96×12 | 5.6858e-3 | 4.6453e-3 | 0.5190 % | 0.1043 % |
  | 144×18 | 2.5319e-3 | 2.0069e-3 | 0.2311 % | 0.1067 % |

  - Iterative uncertainty at 144×18 is 0.043 % of the velocity error and 0.8 % of the dp/dx error.
  - Reference uncertainty is 0 (exact).
  - The extraction is exact for the linear pressure (DRIFT-001 §2).
  - Every margin is ≥ 1.17×, which is far above those uncertainties.

## 3. W8B-2 — StructuredQuad grid-convergence assertions

| original assertion | disposition | derivation |
|---|---|---|
| velocity pair order ≥ 1.5 | **kept unchanged** | Asymptotic on this family once stationary: 2.108 / 2.070 on the test's pairs, 2.029 at 144→216, 2.092 / 2.033 at r = 2 (DRIFT-001 §3). |
| dp/dx pair order ≥ 1.5 | **removed as an order assertion**; dp/dx magnitude is bounded at **every** grid by W8B-1 | The error is `+1.2/(2ny²+1)` (the orthogonal term, UC-001) plus a distortion term of opposite sign. At ny = 8 the error is −3.735e-3 against an orthogonal term of +9.30e-3. It crosses zero between ny = 8 and 12, so no order is defined across the crossing (2.70) or just after it (−0.05). The distortion term fades faster: its ratio to the orthogonal term goes 0.30 → 0.70 → 0.94 by ny = 32, so these grids are pre-asymptotic for dp/dx by construction, not by accident. |
| dp/dx GCI brackets its true error | **removed**; dp/dx is bounded by W8B-1 at the finest grid (0.2311 %) | A GCI exists only in the asymptotic range (Roache). NUM-005's own analysis labels this triplet `monotonic_not_asymptotic`, with asymptotic ratio 79.2 against 1 ± 0.1. Its GCI21 of 1.6e-7 is not an uncertainty. The test now prints that status. |

## 4. W8B-3 — the activation test's solution-level check (S1)

- **A3's S1:** "uncorrected ≥ 1.5 × corrected". This was an empirical factor measured at a single
  grid (1.94 before DIFF-002, 1.528 after A2). With the stationary Rhie–Chow case it measures
  **1.495**.
- **Replacement:** the test's own pre-A3 formulation, which A3 dropped only because its reference
  was the stale two-point solution:
  - the corrected 64×8 solution meets MESH-001's accuracy requirement (`expectPoiseuilleGates`,
    W8B-1);
  - the uncorrected one violates it: velocity L2 > 1.5 × `cartesianVelocityL2(8)`.
- **Measured:** uncorrected 1.6331e-2 > 1.2739e-2 ≥ corrected 1.0921e-2.
- **Non-vacuity.** A library whose correction had no effect gives corrected = uncorrected, which
  fails the corrected-side gate. The two-point, far-cell ×2 and sign-flip controls all fail it.

A3's D1–D3 operator-level checks are unchanged.

## 5. W8B-4 — iterative precondition (W8-INV-001 §13 R4), executable

Every observed order still asserted is covered:
- SQ velocity at 144×18;
- MB velocity and G at 18×45.

The test re-solves the finest grid with every outer tolerance ÷100. The re-solve must converge, and
each quantity must move by ≤ 10 % of its own error.

- **Measured:** 0.043 %; 0.681 % and 0.409 %.
- **Linear flux:** the ÷100 re-solves do not converge (FAIL).

## 6. MultiBlock — no assertion changes

With the stationary case, the **original** W8 MultiBlock test passes its frozen thresholds
unchanged: G order 1.950 / 2.031, velocity order 2.063 / 2.066. That was shown both with the
original test (`results/p12-grad-002/drift-001/logs/d1_dryrun_rc_cases.log`) and with W8B-4 added
(`dry_cand`). W8A-4's G envelope is therefore unnecessary and is not introduced.

## 7. Non-vacuity — pre-freeze dry-run, recorded

[tools/dryrun.sh](tools/dryrun.sh) builds isolated trees outside the repo, runs the candidate tests
against each library and case configuration, and logs to `logs/dry_*.log`.

| variant | library | cases | SQ grid-convergence test | MB grid-convergence test | suites |
|---|---|---|---|---|---|
| **cand** | current `143a1dda` | Rhie–Chow | **PASS** | **PASS** | **15 / 15** |
| linear | current | linear (unchanged) | **REJECT**: 144×18 velocity 1.048× bound; velocity order 1.439; W8B-4 ÷100 solve not converged | **REJECT**: G order 1.050; W8B-4 not converged | 13 / 15 |
| nodiff (two-point) | `719d0fc7` | Rhie–Chow | **REJECT**: W8B-1 at every grid (velocity 1.30–1.38× bound; dp/dx 1.85–2.50× bound) | REJECT, but only by MESH-003's frozen rise monotonicity / G4(c). Its G order 1.90 / 1.95 passes, **which is correct for a valid historical scheme**. | 10 / 15 |
| far-cell ×2 | `078668ba` | Rhie–Chow | **REJECT**: W8B-1 at every grid (up to 26.9× bound); velocity order 0.78 / 0.82 | **REJECT**: velocity 0.75 / 0.83, G 0.76 / 0.83 | 7 / 15 |
| sign flip | `1e5fbb7b` | Rhie–Chow | **REJECT**: W8B-1 at every grid (up to 43× bound); velocity order 0.82 / 0.94 | **REJECT**: velocity 0.84 / 0.93, G 0.90 / 0.95 | 6 / 15 |

Findings:

- The corrupted operators are rejected by **magnitude and order**. Their stationary solutions pass
  W8B-4, which confirms that W8B-4 measures convergence and not accuracy.
- W8B-3's replacement check (S1) rejects every control.
- Monotone decrease alone is not relied on anywhere.

## 8. Verdict rule

Amended W8 **PASSES** only if all of the following hold:

- **(a)** The two repository test files are byte-identical to `data/candidate/`.
- **(b)** The case edits are exactly DRIFT-001 F1.
- **(c)** No file under `src/` or `include/` changed; the 8 W8A-listed production files are at
  their frozen hashes.
- **(d)** MESH-001's summary and MESH-003's gate and summary are byte-identical, as are the W8,
  W8-INV-001 and W8A evidence.
- **(e)** From a **fresh authoritative clean-first build**, both W8 tests pass, and
  `StructuredQuadProductionCase.*` and `MultiBlockProductionCase.*` pass entirely. Binary and
  library hashes and exact counts are recorded.
- **(f)** The §7 control outcomes reproduce: every control is rejected as tabulated and current is
  accepted.

Otherwise **FAIL**: record it and **STOP** at the first failed item. No further amendment is
self-authorized.
