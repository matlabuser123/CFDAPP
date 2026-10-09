# P16 — deriving the G2/G3 thresholds from the sweep

Written for: a reviewer checking that the new gate was derived from measurement
and is genuinely stricter than what it replaces.

Follows `methodology.md` §2 exactly, which was frozen before any wall-function
code existed. Raw data: `../mesh_refinement/sweep.csv`, per-mesh profiles in
`../mesh_refinement/<mesh>/profiles.csv`.

---

## Step 1 — the refinement sweep

k-epsilon with wall functions enabled **through the production case path**
(`"wall_functions": true` in `physics.json`, parsed by `CaseReader`, applied by
`CaseBuilder`). Channel Re_bulk 5600, 64 × ny, target Re_τ = 180.

| mesh | ny | y₁⁺ | Re_τ | E_Reτ | u⁺ L∞ | log samples | τ_w | faces outside | floored | iters | G1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ny02 | 2 | 71.14 | 142.36 | 0.2091 | 3.7001 | 2 | 2.585e-3 | 0 | 0 | 470 | **PASS** |
| ny03 | 3 | 50.28 | 150.97 | 0.1613 | 3.0533 | 3 | 2.907e-3 | 0 | 0 | 406 | **PASS** |
| ny04 | 4 | 39.49 | 158.15 | 0.1214 | 2.3745 | 4 | 3.190e-3 | 0 | 0 | 372 | **PASS** |
| ny05 | 5 | 32.74 | 163.90 | 0.0895 | 1.7783 | 5 | 3.426e-3 | 10 | 0 | 360 | reject |
| ny06 | 6 | 28.03 | 168.34 | 0.0648 | 0.8477 | 4 | 3.614e-3 | 128 | 0 | 355 | reject |
| ny08 | 8 | 21.76 | 174.24 | 0.0320 | 0.5499 | 6 | 3.872e-3 | 128 | 0 | 359 | reject |
| ny12 | 12 | 14.88 | 178.64 | 0.0075 | 0.9683 | 10 | 4.071e-3 | 128 | 16 | 379 | reject |

Every mesh converged. No instability, no non-finite field, no failed solve.

## Step 2 — the valid range

G1 is a **per-face** precondition: the frozen methodology says
*"y₁⁺ … at every wall face"*, so the criterion is `facesOutsideValidBand == 0`.

**Valid: ny ∈ {2, 3, 4}.**

*The first run of this sweep got this wrong* and is worth recording. It
evaluated G1 on the **mean** y⁺ and passed ny = 5, whose mean is 32.7 while ten
of its 128 wall faces sit below 30. A mean-based precondition lets a mesh
through on the strength of its better-resolved faces — exactly the averaging a
precondition exists to prevent. Corrected before any threshold was derived.

## Step 3 — comparison against the preserved baseline

| | pre-P16 (64×16, y⁺ 7.5) | best G1-valid (ny04) | change |
|---|---|---|---|
| E_Reτ | **0.333** | **0.1214** | 2.7× better |
| u⁺ L∞ | **14.56** | **2.3745** | 6.1× better |
| y₁⁺ | 7.50 (buffer) | 39.49 (log layer) | now valid |

The u⁺ figures are not strictly like-for-like: the baseline compared against
B = 5.0 and this compares against the model's own B = ln(E)/κ = 5.565, as G3
requires. The gap is far larger than that 0.565 difference.

## Step 4 — repeatability and numerical noise

Three independent process runs of the whole sweep, compared at every printed
digit:

```
run 1  ny04_yplus45  39.493  158.146  0.1214  2.3745
run 2  ny04_yplus45  39.493  158.146  0.1214  2.3745
run 3  ny04_yplus45  39.493  158.146  0.1214  2.3745
```

**Run-to-run noise is exactly zero** — identical for every mesh, not just this
one. The solver is deterministic and nothing in the wall treatment depends on
iteration order over an unordered container (`WallFunctionWiring.IsDeterministic`
and `WallFunctionChannel.IsDeterministic` pin this).

So the noise margin is **0**, and the thresholds are limited by physics rather
than by scatter. The margin applied below is for platform floating-point
variation (a different compiler or CPU could move the last digits, not the
third), not for run-to-run noise, which does not exist here.

## Step 5 — the thresholds

Worst value over the valid range, plus a 5 % margin:

| Gate | Worst over {ny02, ny03, ny04} | +5 % | **Proposed threshold** |
|---|---|---|---|
| G2 `E_Reτ` | 0.2091 (ny02) | 0.2196 | **0.22** |
| G3 `E_{u⁺,L∞}` | 3.7001 (ny02) | 3.885 | **3.9** |

Both are comfortably inside the mandatory ceilings — G2 < 0.270 and G3 < 9.22 —
so the new gate is strictly stronger than the baseline it replaces: **1.23×
tighter on Re_τ and 2.4× tighter on the velocity profile.**

### A caveat I am not resolving unilaterally

**ny = 2 dominates both thresholds.** It is a two-cell channel — one cell per
half-height, two log-layer samples — and it satisfies G1 only because a very
coarse mesh pushes y₁⁺ *up*. Excluding it would give G2 = 0.17 and G3 = 3.2,
materially tighter.

I have not excluded it, for two reasons: the frozen methodology says "the worst
value observed over the valid range", and dropping the mesh that sets the
threshold in order to get a better threshold is precisely the move that makes a
gate meaningless. If a minimum core-resolution precondition belongs in G1, that
is an amendment to the methodology and your call, not mine.

---

## The finding that matters most

**Accuracy and validity move in opposite directions across this family.**

```
ny:        2      3      4      5      6      8     12
E_Reτ:  0.209  0.161  0.121  0.089  0.065  0.032  0.008    <- improving
y₁⁺:    71.1   50.3   39.5   32.7   28.0   21.8   14.9     <- leaving the band
```

The best numbers in the table (ny = 12: E_Reτ = 0.0075, essentially exact) are
obtained where the high-Re wall function's own derivation does not hold. They
are not trustworthy, and a score-based gate would have selected them.

This is not an implementation defect. At Re_τ = 180 the whole channel is only
180 wall units tall, so "first cell in the log layer" (y₁⁺ ≥ 30 ⟹ ny ≤ 6) and
"enough cells to resolve the core" are in direct conflict. **Re_τ = 180 is a
low-Re DNS benchmark and is marginal for a high-Re wall function** — it is the
benchmark the frozen methodology specifies, so it is what was run, but the
tension is a property of the case, not of the model.

It is also the clearest possible demonstration of why G1 is a **precondition
and not a score**, which the methodology committed to before any of this was
measured.

Corroborating detail: ny = 12 is the only mesh with floored-viscosity faces
(16). The μ_t ≥ 0 floor fires in the buffer layer because the log law there
predicts less shear than a laminar wall — the mechanism the unit tests
predicted, now observed end to end in a production solve.
