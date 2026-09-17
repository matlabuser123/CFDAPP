# P12-DIFF-002-UC-001 — Acceptance Gate (FROZEN before any authoritative test was modified)

Frozen: 2026-09-16. Investigation: `results/p12-diff-002/uc-001/`.
Library under test: `build/release/src/libcfdcore.a`
sha256 `143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a`.

**No production source is modified by UC-001.** Only validation-test reference formulas and their
gate specification change.

## 0. Separation of evidence classes

This gate deliberately keeps five things apart, and never substitutes one for another:

| class | what it is | where it lives |
|---|---|---|
| **analytical reference** | closed forms derived by hand from the stencil coefficients | §1, `tools/uc001_reference.py`, `tools/uc001_parity.py` |
| **independent numerical reference** | exact-`Fraction` solve of the assembled 1D system, no CFDApp code | `tools/uc001_reference.py` |
| **CFDApp production result** | measured output, the *subject* of every comparison | `logs/02,03,05` |
| **literature benchmark** | none used by UC-001 (Poiseuille has a closed form) | — |
| **policy judgment** | which assertions are kept/migrated/redesigned, and why | §4, §5 |

No criterion below uses a CFDApp output as its reference. Every reference side is a formula of
`ny` alone.

## 1. The derived references (analytical; independent of CFDApp)

Second-order one-sided Dirichlet wall reconstruction, `h1 = h/2`, `h2 = 3h/2`:

```
cP = Γ|S| h2 / (h1 (h2 - h1))   cF = Γ|S| h1 / (h2 (h2 - h1))   cB = Γ|S| (1/h1 + 1/h2)
identity  cP - cF = cB          uniform mesh ⇒ cP = 3Γ|S|/h, cF = Γ|S|/(3h), cB = 8Γ|S|/(3h)
```

Fully developed channel at fixed flow rate, `ny` uniform rows, `G = (dp/dx)/μ`:

| | superseded two-point | **DIFF-002 (current)** |
|---|---|---|
| cell-0 equation | `-3u₀ + u₁ = G h²` | `-4u₀ + (4/3)u₁ = G h²` |
| discrete solution | `u_j = -(G/2) y_j (H-y_j) + G h²/8` | `u_j = -(G/2) y_j (H-y_j)` (continuum profile **exactly** at cell centres) |
| `dp/dx` | `dp/dx_exact · ny²/(ny²+2)` | `dp/dx_exact · 2ny²/(2ny²+1)` |
| rel. error vs continuum | `2/(ny²+2)` | `1/(2ny²+1)` |
| drop ratio | `ny²/(ny²+2)` | `2ny²/(2ny²+1)` |

`ny = 8`, `H = 1`, `μ = 1/10`, `U = 1`: DIFF-002 gives `dp/dx = -256/215`, profile
`15/43, 39/43, 55/43, 63/43, …`, sampled centreline `63/43`. The superseded form gives centreline
`16/11` and drop ratio `64/66` — **the exact constants the current tests assert**, which is the
whole defect.

**Centreline sampling parity (new result, `logs/06_parity.log`).** The tests read
`interpolateProfile(profile, H/2)`, not a cell value. For odd `ny`, `y = H/2` is a cell centre;
for even `ny` it is the chord midpoint between the two centre cells. The deficit from the
continuum apex is therefore

```
1.5U - u_c  =  1.5/(2ny²+1)   (ny odd)        4.5/(2ny²+1)   (ny even)   — exactly 3x
```

verified as exact rationals for `ny ∈ {8,12,16,18,24,27,32,36}`. **A centreline order study must
use a parity-consistent triplet.**

## 2. Pre-registered criteria (dry run: `logs/07_gate_dryrun.log`, all controls as expected)

Bounds `1e-4` and `1e-3` are the **pre-existing** bounds of the tests being migrated. **No bound
is loosened by UC-001.** Only the reference *formula* changes.

| id | criterion (reference side is a formula of `ny`) | bound | production | two-point control | corrupted controls |
|---|---|---|---|---|---|
| C1 | `max_j |u_j − u_j^ref(ny)|`, `u_j^ref = -(G_ref/2) y_j (H−y_j)` | 1e-4 | 4.07e-06 / 3.59e-08 / 5.48e-09 (ny 8/12/18) PASS | 8.40e-03 … 3.86e-03 FAIL | 4.5e-02 … 1.1e-01 FAIL |
| C2 | `|dp/dx − dp/dx_ref(ny)| / |dp/dx_ref(ny)|` | 1e-3 | 8.34e-04 / 8.89e-05 / 1.78e-07 PASS | 2.27e-02 … 4.60e-03 FAIL | 5.4e-02 … 2.2e-01 FAIL |
| C3 | `|u_c − u_c^ref(ny)|`, `u_c^ref = 1.5U − k/(2ny²+1)`, `k = 4.5` even / `1.5` odd | 1e-4 | 3.39e-06 PASS | 1.06e-02 FAIL | 5.2e-02 … 1.0e-01 FAIL |
| C4 | `|(1.5U − u_c) − 4.5/(2ny²+1)|` (identity, not a stored number) | 1e-4 | 3.39e-06 PASS | 1.06e-02 FAIL | 5.2e-02 … 1.0e-01 FAIL |
| C5 | `| |dp/dx − dp/dx_exact|/|dp/dx_exact| − 1/(2ny²+1) |` | 1e-3 | 8.27e-04 PASS | 2.26e-02 FAIL | 9.6e-02 … 2.0e-01 FAIL |
| C6 | `| drop/drop_exact − 2ny²/(2ny²+1) |` | 1e-3 | 8.27e-04 PASS | 2.26e-02 FAIL | 1.1e-01 … 2.2e-01 FAIL |

Controls: superseded two-point operator; far-cell coefficient doubled; far-cell sign flipped;
far-cell term dropped. **All four wrong controls must fail every one of C1–C6.** Weakest
non-vacuity margin: C2 at `ny = 18`, two-point exceeds the bound by 4.6x.

### Order gates (constrain the observed order; deliberately NOT operator-discriminating)

| id | criterion | value | verdict |
|---|---|---|---|
| C7 | `dp/dx` asymptotic ratio ∈ [0.9, 1.1] on `ny = 12/18/27` | production 1.0428, reference 0.9972 | PASS |
| C8 | centreline asymptotic ratio ∈ [0.9, 1.1] on the parity-consistent `ny = 8/12/18` | production 0.9936, reference 0.9938 | PASS |
| C9 | `u_profile` L1/L2/L∞ observed order asymptotic (unchanged) | 1.998 / 1.983 / 1.952 | PASS |
| C10 | GCI21 bounds the true error, both quantities (unchanged) | 0.004622 ≤ 0.005852; 0.001541 ≤ 0.001670 | PASS |

C7/C8 do **not** discriminate the operator — the two-point scheme is also second order. C1–C6
carry the entire operator claim. Stated here so the suite's non-vacuity is not overclaimed.

**Why C7 moves off `ny = 8`.** The exact DIFF-002 `dp/dx` sequence on `8/12/18` is asymptotic
(p = 1.9846, ratio 0.9938). Production measures p = 2.2652, ratio 1.1135. Injecting production's
measured pressure-extraction offsets (`+9.93e-04`, `+1.06e-04`, `+2.13e-07`) into the *exact*
reference reproduces 2.2652 / 1.1135 **exactly** (`logs/04_gridorder.log` §C). The same offsets
leave the two-point sequence asymptotic (1.0073), because DIFF-002 shrank the `dp/dx`
discretisation error 4x without changing the extraction error: the offset is 19.3 % of the 8→12
difference Richardson consumes for DIFF-002 versus 5.0 % for two-point. On `12/18/27` the offset
is ≤ 4.6 % of that difference and the gate is valid. This is an instrument-resolution limit, not
a solver defect — and `absoluteNoise` cannot paper over it (raising it only reclassifies the
sequence as `InsufficientSeparation`, which the gate also rejects).

## 3. Accuracy against the continuum (Step 6 — verifying the A6 observation)

| ny | two-point `dp/dx` err | DIFF-002 `dp/dx` err | ratio | two-point `u_c` err | DIFF-002 `u_c` err | ratio |
|---|---|---|---|---|---|---|
| 8 | 3.030e-02 | 7.752e-03 | 3.909 | 4.545e-02 | 3.488e-02 | 1.303 |
| → ∞ | `2/ny²` | `1/(2ny²)` | **4.000** | — | — | **1.333** |

Observed order → **2.000** for both operators (two-point 1.8745→1.9995, DIFF-002
1.9668→1.9999): DIFF-002 changes the error **constant**, not the order.

**The A6 observation is confirmed for the centreline and corrected for `dp/dx`.** A6 recorded
"≈1.30x centreline / ≈3.5x dp/dx". Centreline 1.303 at `ny=8` → 4/3: confirmed. For `dp/dx` the
true discrete ratio is **3.909 at `ny=8` → 4.000**, not 3.5; A6's 3.5 came from production's
*measured* `dp/dx` (0.0086 vs 0.0303), whose 8.3e-4 extraction bias inflates the DIFF-002 error.
Improvement is recorded as a **consequence**, never as an acceptance criterion.

## 4. Step 5 — production vs the independent reference (`logs/02_crosscheck.log`)

| level | what | result |
|---|---|---|
| 1 | assembled momentum row vs hand-derived row, **every entry**, 8 rows | worst \|Δ\| **1.110e-16**, RHS 0.000e+00 |
| 2 | solved 1D profile vs exact rationals, **every cell** | worst \|Δ\| **2.220e-16**; `dp/dx` rel 3.73e-16; centreline 2.22e-16 |
| 3 | matrix structure: far-cell column present, 4 nonzeros/wall row | as derived |
| 4 | `cB` against a **nonzero** wall value (the `u_b = 0` RHS check is vacuous) | \|Δ\| 0.000e+00; two-point `cB` would differ by 2.5e-02 |
| 5 | wall flux + global momentum balance | \|Δ\| 6.94e-17; balance closes to 9.32e-16 |
| 2D | solved 2D profile L∞ vs the reference, ny 8/12/18/27 | 4.07e-06 / 3.59e-08 / 5.48e-09 / 8.23e-09 |

**Verdict: production implements the independently derived DIFF-002 system.** Step 5 passes; no
`PRODUCTION/REFERENCE DISAGREEMENT`.

## 5. Step 8 — classification

| test | classification | reason |
|---|---|---|
| `PoiseuilleValidation.Profile` | **MIGRATE_DERIVED_REFERENCE** | fails only against the superseded two-point reference; matches the derived DIFF-002 reference to 4.07e-06 (bound 1e-4) |
| `PoiseuilleValidation.PressureDrop` | **MIGRATE_DERIVED_REFERENCE** | same, plus two hard-coded two-point constants (`2/66`, `64/66`) replaced by the derived identities |
| `PoiseuilleValidation.ProductionGridConvergence` | **MIGRATE_DERIVED_REFERENCE** + **REDESIGN_VALIDATION** (one gate) | stale reference on 3 grids; *and* `pressure_gradient_asymptotic` is an instrument-resolution failure on `ny = 8` (§2) |
| `PoiseuilleValidation.MassFlow` | **KEEP** | no discrete-exact comparison; passes unchanged |

No `PRODUCTION_DEFECT`, no `UNCERTAIN`.

## 6. Planned edits (only after this file is frozen)

Pre-modification sha256:

```
335cddd7d4f2df566d365000f027983a2a15d5d11d741841e66d3cfa88db0970  PoiseuilleValidationUtils.hpp
fd1c08c465c5120e405d161d1e4aa7e76df3a80d9014b38a74eb1e8be9ae9815  PoiseuilleValidationUtils.cpp
479739ebdcd085209ed9e611d999752737e3954e1740e1eae75c669cf613cfe3  test_poiseuille_production_validation.cpp
c33143bfc886b0695e8a11b852f0035575f8f8389ee24e4d60a47888e863d674  test_poiseuille_validation.cpp  (NOT modified — does not use the discrete reference)
```

1. `PoiseuilleValidationUtils.{hpp,cpp}` — `discretePressureGradient` → `2ny²/(2ny²+1)`;
   `discretePoiseuilleVelocity` → drop the `+G h²/8` offset; rewrite the header derivation.
2. `test_poiseuille_production_validation.cpp` — replace `2.0/66.0` by `1.0/(2ny²+1)` and
   `64.0/66.0` by `2ny²/(2ny²+1)`, both computed from `ny`; add the `ny = 27` grid; compute the
   `dp/dx` order on `12/18/27` and the centreline order on the parity-consistent `8/12/18`;
   record the parity rule and the extraction-bias limitation.

## 7. Pass condition (Step 10)

Fresh rebuild, then: all of C1–C10 hold; `PoiseuilleValidation.{Profile, MassFlow, PressureDrop,
ProductionGridConvergence}` pass; the Poiseuille / momentum / boundary-diffusion / SIMPLE / MMS
suites and the relevant W7 subset show **no new failures**. All three U-C failures resolved.

## 8. Recorded limitations

- C2/C5/C6 at `ny = 8` pass with only **1.2x** margin, bounded by the deterministic
  pressure-extraction bias (8.3e-04 relative; the solve is bit-reproducible — see
  `Grid64x8IsDeterministic`). The bound is the pre-existing 1e-3 and is not changed here.
- C7's triplet adds `216x27` (~27 s release), raising the grid study's runtime.
- C7/C8 are not operator-discriminating (§2).
- The `ny = 27` grid is odd and must never enter the centreline order triplet (§1).
