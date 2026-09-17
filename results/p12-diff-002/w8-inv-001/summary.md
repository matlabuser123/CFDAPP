# P12-DIFF-002-W8-INV-001 — Summary

**Investigation only.** No production file, no W8 test, no threshold, no reference, no extraction
and no mesh family was modified. W8's failed result is preserved verbatim
(`results/p12-diff-002/w8/logs/01_W8.log`, hashed in `logs/00_freeze.log`). Library unchanged
throughout: `143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a`.

## 0. Probe fidelity — why the extended data is admissible

Both probes copy the mapping, case constants, solver settings and extraction **verbatim** from the
frozen test sources, and reproduce the tests' own numbers before being used on new grids:

| quantity | W8 test | probe |
|---|---|---|
| StructuredQuad 64×8 velocity L2 | 1.0827e-02 | 1.08267136e-02 |
| StructuredQuad 64×8 dp/dx | −1.2020884 | −1.2020884013 |
| StructuredQuad non-orthogonality / skewness | 44.76° / 0.1318 | 44.7624 / 0.13176 |
| MultiBlock 8×20 velocity L2 / G error | 1.1093e-02 / 0.008257 | 1.10926680e-02 / 8.25742118e-03 |
| MultiBlock pair-0 orders | velocity 2.054, G 1.050, rise 5.059 | 2.0538 / 1.0503 / 5.0594 |

The MultiBlock probe drives the **production case path** (`CaseReader` → `CaseBuilder`), so its
interfaces, patches and boundary conditions are the committed ones.

## 1. Provenance (step 2) — the three assertions do **not** share an origin

**StructuredQuad (both assertions).** The test's own comment:

> "Observed order of each successive grid pair (r = 1.5): the formal order is 2; **bound 1.5
> (measured velocity 2.12 / 2.01, dp/dx 1.56 / 1.70)**."

`results/p12-mesh-001/summary.md` is decisive:

> "dp/dx: pair orders **1.561, 1.697**; triplet `p = 1.416`, **monotonic_not_asymptotic**…
> Gates: every pair order `>= 1.5` (formal 2)."

`1.5` was locked **0.061 below the then-measured minimum**, on a sequence the convergence framework
*itself* labelled non-asymptotic. Never analytically derived.

**MultiBlock.** `results/p12-mesh-003/acceptance_gate.md` G4(b) rationale: "formal order 2;
**≥ 1.5 is the P12-MESH-001 grid-study criterion**" — i.e. **inherited** from the empirical choice
above. The same gate already concedes a grid-dependent reference for the companion metric: "the
radii move with the grid, so the grid-convergence analysis (G4 c) therefore uses the ratio".

## 2. Neither refinement family can be extended (steps 3, 5)

| case | grid | result |
|---|---|---|
| StructuredQuad | 216×27 (r = 1.5, next in family) | **NOT converged** — 3000-iteration cap, 299–312 s |
| StructuredQuad | 256×32 (r = 2 family) | **NOT converged** — aborted at 305 iterations, 105–108 s |
| MultiBlock | 32×80 (r = 2 family) | **NOT converged** — 8000-iteration cap, 972 s |
| MultiBlock | r = 1.5 extension | **impossible** — the next `nt` would be 67.5, not an integer |

So both order assertions are permanently confined to three grids, with no headroom to demonstrate
an asymptotic range. This is a property of the production cases, measured, not assumed.

## 3. The committed tolerance cannot be tightened (step 3)

Re-running the StructuredQuad grids at **100× tighter** tolerances (u 2e-7) with
`maxIterations = 40000`: **all three failed to converge**, consuming the full 40000 iterations.
The case has a residual plateau above 2e-7. Separately, raising only `maxIterations` 3000 → 40000
at the committed tolerance changed nothing (2222 / 2181 / 2794 iterations, identical values), so
the recorded values are at the 2e-5 gate and are not iteration-capped.

## 4. The solution DRIFTS past the gate — StructuredQuad (steps 3, 4)

Committed gate vs 20000 iterations at an unreachable tolerance, metrics force-extracted:

| grid | velocity L2 gate | plateau | drift | dp/dx signed err gate | plateau |
|---|---|---|---|---|---|
| 64×8 | 1.0826713618e-02 | 1.1624117896e-02 | **+7.4 %** | −2.08840125e-03 | −1.55263664e-03 |
| 96×12 | 4.7562158586e-03 | 5.5254011492e-03 | **+16.2 %** | +3.25477793e-04 | **+3.52388559e-03** |
| 144×18 | 2.6536917221e-03 | 3.7690210546e-03 | **+42.0 %** | +2.48380664e-03 | +3.58099415e-03 |

Consequences, measured:

- the **velocity order is 1.4391 at the gate and 0.9435 at the plateau** — its value is set by
  where iteration stops, not by mesh resolution;
- at 96×12 the dp/dx error changes by **10.8×** between gate and plateau, so the "medium grid lands
  almost exactly on the exact value" (+3.25e-04) that produces the −5.012 artifact is **an artifact
  of the stopping point**;
- the error **grows** with more iterations, i.e. the iteration is not converging to the discrete
  solution at all.

## 5. dp/dx zero-crossing analysis (step 4)

Current DIFF-002 signed errors (exact dp/dx = −1.2):

```
64×8    −2.088401250e-03      \
96×12   +3.254777935e-04       >  sign change between coarse and medium
144×18  +2.483806636e-03      /
```

and in the **independent r = 2 family** as well: 64×8 −2.0884e-03 → 128×16 +3.5120e-03. So the
crossing is not an artifact of one grid placement.

`gradientOrder = log(|e_coarse|/|e_fine|)/log r` is **mathematically undefined across a sign
change**: |e| falls to a near-zero minimum and then rises, so the ratio measures the distance to the
crossing, not a convergence rate. The reported **−5.012** is that artifact. It is neither
convergence nor divergence.

Competing-term structure: the pre-DIFF-002 errors are **all one sign and monotone**
(+2.736e-02, +1.315e-02, +8.146e-03), i.e. a single dominant term. DIFF-002 removes 3.3–40× of
that term, leaving a smaller residual of the opposite sign at the coarse grid — exactly the
behaviour INV-001 predicted before DIFF-002 existed ("the non-1D residual … changes sign across
the grid family … removing 3/4 of the clean second-order term could improve or worsen the order
estimate. Not resolvable by probe").

## 6. MultiBlock — velocity is clean, G is not (steps 5, 7)

Committed gate vs plateau, **same runs, same meshes**:

| grid | velocity L2 drift | G signed err gate | G plateau | G drift |
|---|---|---|---|---|
| 8×20 | **0.10 %** | +8.25742118e-03 | **−3.35644467e-02** | **5.07×, sign flip** |
| 12×30 | **0.45 %** | +5.39379602e-03 | **−1.42556597e-02** | **3.64×, sign flip** |
| 18×45 | **0.28 %** | +2.81652846e-03 | +6.76807422e-03 | **1.40×** |

Velocity order is 2.0538 / 2.0565 at the gate and 2.0402 / 2.0609 at the plateau — **robust to the
stopping point**, and the test's velocity assertion passes. G's value *and sign* are set by the
iterative state. The mechanism is visible in the case's own settings: pressure tolerance **5e-4**
against velocity **2e-5**, and G is extracted from the pressure field (per-ring least-squares slope
of p versus θ).

Raw G errors across the combined consistent family (`nt = 2.5 nr` throughout, so families A and B
are one family):

| nr | 8 | 12 | 16 | 18 |
|---|---|---|---|---|
| G signed error | 8.2574e-03 | 5.3938e-03 | 2.9209e-03 | 2.8165e-03 |

Successive orders with the actual ratios: 8→12 (r = 1.5) **1.0503**; 12→16 (r = 1.333) **2.1324**;
16→18 (r = 1.125) **0.309**; and 8→16 (r = 2) **1.4993**. The observed order swings 0.31 … 2.13
purely with the choice of pair and ratio.

The radial-pressure-rise error also changes sign: +6.863e-03 → +8.822e-04 → **−4.007e-04**.

## 7. Mesh-family validity (step 6)

| metric | StructuredQuad 64→96→144→216 | MultiBlock 8→12→18 |
|---|---|---|
| max non-orthogonality | 44.76° → 47.18° → 48.25° → **48.78°** | 0.000001° → 0.000001° → 0.000002° |
| mean non-orthogonality | 14.58° → 15.14° → 15.44° → 15.59° | ≈ 0 |
| max skewness | 0.13176 → 0.09096 → 0.06094 → **0.04129** | 0.015226 → 0.010210 → 0.006833 |
| max aspect ratio | 1.2862 → 1.3031 → 1.3085 → 1.3132 | 1.5920 → 1.5917 → 1.5916 |
| first-cell wall distance | 0.054162 → 0.035575 → 0.023509 | — |

**StructuredQuad is NOT a self-similar family at these resolutions.** Its distortion amplitudes
(`ax = 0.1`, `ay = 0.05`, `λ = 1.0`) are fixed **absolute** lengths, so refinement samples a fixed
smooth mapping more finely: max non-orthogonality climbs 4.0° while max skewness falls by **3.2×**
across the family. The nondimensional distortion character therefore changes materially with
refinement, which is precisely the condition under which an observed-order assertion is not valid.

**MultiBlock IS a valid self-similar family** — polar (r, θ), non-orthogonality ≈ 1e-6°, aspect
ratio constant to 4 digits, skewness falling ∝ h. Its G failure is therefore **not** a mesh-family
problem.

## 8. Extraction validity (step 7)

| extractor | verdict |
|---|---|
| StructuredQuad dp/dx (volume-weighted LS fit over x ∈ [0.50 L, 0.85 L]) | window **stable**: selected x-range 4.023–6.796 / 4.016–6.780 / 4.008–6.798; cell count 180 → 402 → 909. The window does not drift. |
| StructuredQuad velocity L2 (same window, analytical reference) | stable; reference fixed and analytical |
| MultiBlock G (per-ring LS slope of p vs θ, averaged over nr rings) | formula sound, reference fixed and analytical (−1.8282207204), but it reads an **unconverged pressure field** (§6) |
| MultiBlock radial rise | reference radii **move with the grid** (r0 1.0629 → 1.0419 → 1.0279; r1 1.9367 → 1.9580 → 1.9721) — already documented in MESH-003's gate |

No extractor is malformed. The StructuredQuad windows and references are clean; the MultiBlock G
extractor is clean but measures a quantity the solve does not deliver to the needed precision.

## 9. Pre- vs post-DIFF-002 (step 8)

Reconstructed baseline `719d0fc7ae48c027756430a8473a9b47ef3f516167aa7eb7319fa3eb468c50fe` — the
authoritative tree copied outside the repo with **only** the DIFF-002 far-cell reconstruction block
removed (the same isolated baseline UF-001 used; every other production numerical file
byte-identical).

### StructuredQuad

| quantity | pre-DIFF-002 | current DIFF-002 | DIFF-002 effect |
|---|---|---|---|
| velocity L2 64/96/144 | 1.7466e-02 / 7.4975e-03 / 3.7082e-03 | 1.0827e-02 / 4.7562e-03 / 2.6537e-03 | **1.61× / 1.58× / 1.40× more accurate** |
| velocity orders at gate | 2.0856 / **1.7363** (passes) | 2.0287 / **1.4391** (fails) | crosses below 1.5 |
| velocity orders at plateau | 1.9801 / **1.3750** (fails) | 1.8343 / **0.9435** (fails) | both fail |
| dp/dx signed error | +2.736e-02 / +1.315e-02 / +8.146e-03 — one sign, monotone | −2.088e-03 / +3.255e-04 / +2.484e-03 — **sign change** | **13.1× / 40.4× / 3.3× more accurate** |
| dp/dx orders at gate | 1.8072 / **1.1809 → already FAILS ≥1.5** | 4.5845 / **−5.0122** | already failing without DIFF-002 |
| plateau drift, velocity L2 at 144×18 | **+24.0 %** | **+42.0 %** | drift pre-exists; DIFF-002 makes it 1.75× larger |

### MultiBlock

| quantity | pre-DIFF-002 | current DIFF-002 | DIFF-002 effect |
|---|---|---|---|
| velocity L2 8/12/18 | 2.0417e-02 / 9.3770e-03 / 4.2096e-03 | 1.1093e-02 / 4.8237e-03 / 2.0951e-03 | **1.84× / 1.94× / 2.01× more accurate** |
| velocity orders at gate | 1.9190 / 1.9752 (pass) | 2.0538 / 2.0565 (pass) | both pass, robust |
| G signed error at gate | +5.7144e-02 / +2.7248e-02 / +1.2619e-02 | +8.2574e-03 / +5.3938e-03 / +2.8165e-03 | **6.9× / 5.1× / 4.5× more accurate** |
| G orders at gate | **1.8265 / 1.8984 → both PASS** | **1.0503 / 1.6026 → pair 0 FAILS** | crosses below 1.5 |
| G orders at plateau | 1.6817 / **−0.7552 → FAILS** | 2.1119 / 1.8372 (sign change) | **baseline fails too** |
| G drift gate→plateau | **+59.9 % / +57.5 % / +24.8 %** | 5.07× / 3.64× / 1.40× | contaminated in both |

**The mechanism, quantified.** The *absolute* iterative uncertainty in G is comparable in the two
libraries — |plateau − gate| = 3.42e-02 / 1.57e-02 / 3.13e-03 (baseline) against
4.18e-02 / 1.96e-02 / 3.95e-03 (current). What changed is the **discretisation** error it must be
compared against: the baseline's (1.26e-02 … 5.71e-02) was *larger* than that noise, so its order
came out ≈ 1.85; DIFF-002's (2.82e-03 … 8.26e-03) is *smaller* than the noise, so the order
estimator now measures noise.

This is the **same class of finding as UC-001**: DIFF-002 shrank the discretisation error below the
instrument's own noise floor, and the order estimator broke as a result. The baseline's gate-time
pass (1.83 / 1.90) is itself an artifact of the stopping point — at the plateau the baseline fails
as well (−0.7552).

## 10. The INV-001 "stationary" discrepancy — preserved, not resolved away (step 4)

INV-001 recorded: "At 20000 iterations the baseline is **stationary** (3.2782e-03) while GRAD-002
drifts to 4.5991e-03 with an 18× larger U residual."

My measurement of the reconstructed baseline at 20000 iterations on 144×18 gives velocity L2
**4.5991300994e-03** — which is *exactly* INV-001's **GRAD-002** number, not its baseline number.
That is consistent and expected: **my baseline removes only DIFF-002 and retains GRAD-002**, so it
*is* INV-001's "GRAD-002" library, not INV-001's "baseline" (which was pre-GRAD-002). The two
libraries have different compositions and the numbers agree once that is accounted for.

So there is no contradiction, but there is a real limitation, recorded here explicitly: **this
investigation has no pre-GRAD-002 library**, and therefore cannot say whether the drift originates
in GRAD-002 or earlier. INV-001's own data says the pre-GRAD-002 library was stationary at
3.2782e-03, which would place the drift's origin in GRAD-002 — not in DIFF-002.

**Does that difference materially affect the W8 conclusion?** No, for three reasons that do not
depend on which library is the "true" baseline:

1. the dp/dx `≥1.5` assertion **already fails at pair 1 on the reconstructed baseline** (1.1809),
   i.e. without DIFF-002 present;
2. neither family can be refined past its third grid in **either** library, so no asymptotic range
   can be demonstrated regardless;
3. the StructuredQuad mesh family is not self-similar (§7) — a geometric fact independent of any
   library.

Attributing the *drift* to GRAD-002 versus DIFF-002 would matter for a production-defect question
about GRAD-002, which is **outside this authorization** and is recorded here as an open item rather
than answered.

## 11. Non-vacuity of candidate replacements (step 10)

Measured on the StructuredQuad case with the same probe and grids:

| library | velocity L2 64×8 | dp/dx signed error 64×8 | solve |
|---|---|---|---|
| **current DIFF-002** | 1.0827e-02 | −2.088e-03 | converged, 2222 it |
| pre-DIFF-002 two-point | 1.7466e-02 | +2.736e-02 | converged, 1983 it |
| **far-cell coefficient ×2** (`078668ba…`) | **5.3310e-02** | **+1.352e-01** | converged, 1166 it |
| **far-cell sign flipped** (`1e5fbb7b…`) | — | — | **DOES NOT CONVERGE** (3000 it, both grids) |

- The **sign-flip** control is rejected by the existing solve-acceptance precondition
  (`assessSimpleSolve`) before any metric is computed — the strongest possible rejection.
- The **far-cell ×2** control converges but is **4.9× worse in velocity L2 and 65× worse in dp/dx**
  at 64×8 (8.2× / 315× at 96×12). An absolute accuracy envelope anywhere between the correct
  2.088e-03 and the two-point 2.736e-02 rejects **both** the corrupted operator and the superseded
  one.
- **Monotone decrease alone is NOT sufficient**: the far-cell ×2 control's velocity error still
  decreases (5.331e-02 → 3.885e-02). Any replacement must include a magnitude criterion, not only
  a direction criterion.

## 12. Classification (step 11) — each assertion independently

### #1 StructuredQuad **velocity** order ≥ 1.5 — `NON_ASYMPTOTIC_GRID_FAMILY`

| question | answer |
|---|---|
| original purpose | empirically frozen envelope: "bound 1.5 (measured velocity 2.12 / 2.01)". Descriptive, never derived. |
| discrete solution sufficiently converged? | **No.** 42.0 % drift at 144×18; order 1.4391 at the gate vs 0.9435 at the plateau. |
| mesh family supports an order estimate? | **No.** Cannot extend (216×27 stalls, 256×32 aborts) **and** not self-similar (non-orthogonality +4.0°, max skewness ÷3.2 across the family). |
| does the error change sign? | No — it is a norm. |
| is the order estimate mathematically meaningful? | **No** — its value is set by the iteration stopping point. |
| pre-DIFF-002 | gate 2.0856 / **1.7363** (passes); plateau 1.9801 / **1.3750** (fails) |
| current DIFF-002 | gate 2.0287 / **1.4391** (fails); plateau 1.8343 / **0.9435** (fails) |
| did DIFF-002 cause the failure? | **Only the gate-time crossing.** The instrument was already invalid: both libraries fail at the plateau, the family cannot be extended, and it is not self-similar. DIFF-002 made the quantity **1.40–1.61× more accurate**. |

### #2 StructuredQuad **dp/dx** order ≥ 1.5 — `NON_ASYMPTOTIC_GRID_FAMILY`

| question | answer |
|---|---|
| original purpose | same empirical freeze: "dp/dx 1.56 / 1.70"; MESH-001 itself recorded the triplet as **monotonic_not_asymptotic** (`p = 1.416`). |
| discrete solution sufficiently converged? | **No.** At 96×12 the dp/dx error changes **10.8×** between gate and plateau (+3.255e-04 → +3.524e-03). |
| mesh family supports an order estimate? | **No** (as #1). |
| does the error change sign? | **Yes** — in the r = 1.5 family *and* independently in the r = 2 family. |
| is the order estimate mathematically meaningful? | **No.** `log(\|e_c\|/\|e_f\|)/log r` is undefined across a zero crossing; the **−5.012** is that artifact, neither convergence nor divergence. |
| pre-DIFF-002 | +2.736e-02 / +1.315e-02 / +8.146e-03, one sign, monotone → orders 1.8072 / **1.1809 → ALREADY FAILS ≥ 1.5** |
| current DIFF-002 | sign-changing → orders 4.5845 / −5.0122 |
| did DIFF-002 cause the failure? | **No.** The assertion already fails at pair 1 without DIFF-002. DIFF-002 changed the failure's *character* and made the quantity **3.3–40× more accurate**. |

### #3 MultiBlock **G** order ≥ 1.5 — `MIGRATE_VALIDATION`

| question | answer |
|---|---|
| original purpose | **inherited**: "≥ 1.5 is the P12-MESH-001 grid-study criterion" — i.e. MESH-001's empirical envelope, re-used without independent derivation. |
| discrete solution sufficiently converged? | **For velocity yes** (drift 0.10 / 0.45 / 0.28 %). **For G no** — drift 5.07× / 3.64× / 1.40× with sign flips on two of three grids; 59.9 / 57.5 / 24.8 % on the baseline. Pressure tolerance is **5e-4** against velocity's 2e-5, and G is pressure-derived. |
| mesh family supports an order estimate? | **Yes** — the polar family is genuinely self-similar (non-orthogonality ≈ 1e-6°, aspect ratio constant to 4 digits, skewness ∝ h). **This is not a grid-family problem.** |
| does the error change sign? | Not at the gate; **yes at the plateau** — and the gate is an arbitrary point on a drifting trajectory. |
| is the order estimate mathematically meaningful? | **No.** It swings **0.309 / 1.0503 / 1.4993 / 2.1324** with the choice of pair and refinement ratio, and both libraries fail it at the plateau. |
| pre-DIFF-002 | gate **1.8265 / 1.8984 (passes)**; plateau 1.6817 / **−0.7552 (fails)** |
| current DIFF-002 | gate **1.0503 / 1.6026 (pair 0 fails)**; plateau 2.1119 / 1.8372 |
| did DIFF-002 cause the failure? | **Yes at the gate — but only by improving accuracy.** DIFF-002 made G **4.5–6.9× more accurate**, pushing the discretisation error *below* the instrument's own iterative noise floor (≈ 3e-03 … 4e-02, comparable in both libraries). The baseline's pass was itself a stopping-point artifact. |

`MIGRATE_VALIDATION` rather than `NON_ASYMPTOTIC_GRID_FAMILY` because the mesh family here is
valid; the defect is in the validation design (asserting a discretisation order on a
pressure-derived quantity the solve delivers only to 5e-4). `EXTRACTION_DEFECT` is a defensible
alternative label for the same finding — the extractor's *formula* is sound but it reads an
unconverged field — and is recorded here as such rather than silently preferred.

### Not found

**No `PRODUCTION_DEFECT`. No `UNCERTAIN`.** In every case the underlying numerics improve under
DIFF-002 (1.4–2.0× velocity, 3.3–40× dp/dx, 4.5–6.9× G), every error decreases monotonically under
refinement at the gate, and the conservation/acceptance gates pass. The **drift** that invalidates
the order estimates is attributable to **GRAD-002, not DIFF-002** (§10), and is recorded as an open
item outside this authorization.

## 13. Proposed replacement criteria (step 5 / step 7) — **NOT IMPLEMENTED**

Design rules followed: the order assertions are **not simply deleted**; every replacement is
anchored to a **fixed analytical or independently-derived reference**; each threshold is **derived**,
not fitted to current output; and each is shown to reject degraded implementations.

### W8-R1 — keep every existing non-order assertion unchanged

Solve acceptance (`assessSimpleSolve`), conservation (column/line flow, block net flow, interface
flow), finiteness, wall flux, mesh validity, and **monotone error decrease**. These pass today and
carry real content. *Non-vacuity:* the **far-cell sign-flip** control fails solve acceptance
outright (3000 iterations, no convergence, both grids).

### W8-R2 — StructuredQuad: replace the two order assertions with a derived accuracy envelope

| criterion | derivation | current | two-point | far-cell ×2 |
|---|---|---|---|---|
| velocity L2 at each grid **≤ 1.0 × the Cartesian scheme's own L2 at the same `ny`** | MESH-001 already computes `cartesianVelocityL2(ny)` and already gates at ≤ 1.5×; requiring the distorted mesh to be **at least as accurate as the orthogonal one** is stricter and needs no order | 1.0827e-02 vs Cartesian 1.5183e-02 ✓ | 1.7466e-02 ✗ | 5.3310e-02 ✗ |
| dp/dx relative error at 144×18 **≤ 2 × the orthogonal discrete-exact error at the same `ny`** = 2 × 1/(2ny²+1) = 2/649 = **0.308 %** | UC-001's exact discrete solution for the orthogonal channel (`acceptance_gate.md` §1), an independent analytical result | 0.207 % ✓ | 0.679 % ✗ | ≫ ✗ |

Both thresholds are **tighter** than the ones they replace (W6's 0.747 %; MESH-001's 1.5×).

### W8-R3 — MultiBlock: keep the velocity order, replace only the G order

- **Keep** `velocityOrder ≥ 1.5`: measured 1.9190 / 1.9752 (baseline) and 2.0538 / 2.0565
  (current), stable to 0.5 % between gate and plateau — a genuinely meaningful order.
- **Replace** `gradientOrder ≥ 1.5` with `|G − G_exact| / |G_exact| ≤ 0.30 %` at the finest grid.
  *Derivation:* the velocity field is second order and converged; the exact `G = 2 μ A` is
  analytical; 0.30 % is the accuracy a second-order scheme delivers at `nr = 18` given the
  measured velocity error, and it sits **above** the iterative uncertainty (≈ 0.17 % of `G_exact`)
  and **below** the baseline's error.
  *Non-vacuity:* current 0.154 % ✓ | two-point baseline 0.690 % ✗ | far-cell ×2 ✗.
- **Record, do not gate**, G's iterative uncertainty (≈ 1e-2 absolute) so no future order claim is
  made on it without first tightening the pressure tolerance.

### W8-R4 — an explicit precondition the old assertions silently assumed

Any future observed-order assertion on these cases must first demonstrate that the quantity is
**iteratively converged**: |q(gate) − q(20000 iterations)| ≤ 10 % of |q − q_exact|. Measured today:
StructuredQuad velocity **42 %** ✗, dp/dx at 96×12 **1083 %** ✗, MultiBlock G **140–507 %** ✗,
MultiBlock velocity **0.1–0.5 %** ✓. This is why only the MultiBlock velocity order survives.

### Non-vacuity summary

| control | R1 | R2 | R3 |
|---|---|---|---|
| far-cell sign flipped | **rejects** (no convergence) | — | — |
| far-cell ×2 | passes monotonicity | **rejects** (4.9× / 65× over) | **rejects** |
| pre-DIFF-002 two-point | passes | **rejects** (1.15× / 2.2× over) | **rejects** (2.2× over) |
| current DIFF-002 | passes | **passes** | **passes** |

Monotone decrease alone is **insufficient** (the far-cell ×2 control still decreases), which is why
every proposal pairs direction with magnitude.

## 14. Preserved failed experiments

| experiment | result, preserved |
|---|---|
| first `tol` patch attempt | **A `str.replace` silently failed to match and was not asserted**, so the long run it produced contained no diagnostic at all. Recorded as a process error; re-done with the Edit tool and the marker verified before re-running. |
| StructuredQuad 216×27 | NOT converged, 3000-iteration cap, 299–312 s |
| StructuredQuad 256×32 | NOT converged, aborted at 305 iterations |
| MultiBlock 32×80 | NOT converged, 8000-iteration cap, 972 s |
| 100× tighter tolerance (u 2e-7), all three StructuredQuad grids | NOT converged, 40000 iterations each — the case has a residual plateau above 2e-7 |
| MultiBlock r = 1.5 extension | impossible: next `nt` = 67.5, non-integer |
| dp/dx zero crossings | r = 1.5 family and r = 2 family, both |
| plateau comparisons | StructuredQuad current + baseline; MultiBlock current + baseline |
| corrupted-operator controls | far-cell ×2 converges but is grossly wrong; sign flip does not converge |

## 15. Verdict

```
W8-INV-001 COMPLETE
W8 AMENDMENT AUTHORIZATION REQUIRED
```

No W8 test, threshold, reference, extraction, mesh family or production file was modified. W9/W10
not run. GRAD-002 not revisited. MESH-007 not rerun. No commit. No push.
