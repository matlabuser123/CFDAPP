# GPU-DISC-001P — Phase C: null and unreachable controls, with proofs

Ten controls across GPU-DISC-001 cannot change any result under the production contract. They are
**excluded from the detected-observable denominator** — counting them as failures would be wrong,
and counting them as successes would be worse.

Every one of them is still **executed** on every campaign. The drivers treat a control declared
`null` that *is* detected as a **run failure**, so each claim below is checked rather than asserted.

```text
PROVABLY NULL                              8   algebraic identity, exact zero, or dead value
UNREACHABLE UNDER VALID PRODUCTION CONTRACT 2   branch no valid generated mesh can enter
```

A control is **not** null merely because it "looks harmless", and the counter-example below is the
reason this document exists at all.

---

## The counter-example: 001K k11, a null claim that was wrong

`k11` short-circuits a boundary face whose coupling coefficient is exactly `0.0`:

```cpp
// production
const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);
Real value = predictorMassFlux[f] + fluxCorrection;

// k11
Real value = faceCoefficient[f] == 0.0 ? predictorMassFlux[f]
                                       : predictorMassFlux[f] + fluxCorrection;
```

The reasoning was: multiplying by exactly `0.0` gives `0.0`, and adding `0.0` changes nothing, so
the branch reaches the same answer by a shorter route. **That is false**, and the driver caught it:

> `x + (+0.0) == x` for every finite `x` **except** `x = -0.0`, which becomes `+0.0`.

Production evaluates `-0.0 + (+0.0)` and gets `+0.0`; the branch returns `-0.0` untouched. Thirteen
faces, `maxAbs = 0`, bitwise different. So `k11` is **observable** and is counted.

The practical rule that follows, and that 001K adopted: **reproduce the CPU's arithmetic rather
than branching around a known no-op.** Every proof below is written to survive this test — each one
either names why the `-0.0` case cannot arise, or does not depend on additive identity at all.

---

## Provably null — 8

### 1. 001D NC1 — `Ff >= 0.0` → `Ff > 0.0` (zero flux picks the neighbour)

*Category: multiplication by exact zero.*

At exactly `±0.0` flux the mutation selects a different upwind cell, so `phiFace` genuinely
changes. But every use of that value is multiplied by the flux:

* **operator** — the contribution is `cellFlux * phiFace` with `cellFlux = ±0.0`, so the product is
  `±0.0` for any finite `phiFace`;
* **assembly** — `effectiveFlux = cp * (±0.0) = ±0.0`, so the mutation only moves a `±0.0` between
  the diagonal, an off-diagonal, or the RHS. `SparseMatrixBuilder::build()` drops an entry whose
  accumulated value is exactly `0.0` anyway.

The `-0.0` trap does not apply: the *sign* of the zero product is fixed by `cellFlux`, which the
mutation does not touch — it changes which `phiFace` is multiplied, not the flux.

**Consequence, stated rather than hidden:** the differential does not verify the `>=` / `>` boundary
of the upwind predicate, because no observable depends on it. That code matches the CPU by
transcription, not by test. The real owner/downwind control is NC7 (`d6`), which swaps the upwind
side on *every* face including the non-zero-flux ones, and is detected in 945 of 1684 cases.

### 2. 001D M6 — 2D skew-corrected face value carries the interpolated z

*Category: value written but never read in the active dimension.*

`greenGaussSumKernel` reads the face-velocity z component only inside its `threeDimensional`
branch. On a 2D mesh that branch never executes, so `fz[f]` is written and never read. The CPU's
exact `0.0` and any other value give identical results.

This is a *dead value*, not an additive identity, so the `-0.0` question does not arise.

### 3. 001I G6n — the matrix-side FixedValue filter removed

*Category: multiplication by exact zero.*

Two mechanisms keep a non-FixedValue boundary face out of the pressure-correction matrix: the
matrix-side type filter, and `faceTermsKernel` zeroing `faceCoefficient[f]`. Removing the filter
leaves `value += faceCoefficient[f]` with `faceCoefficient[f]` exactly `+0.0`.

The `-0.0` trap is checked and does not apply: the accumulator `value` starts at `+0.0` and every
contribution reaching it on such a row is a coupling coefficient, none of which is `-0.0` — so no
`-0.0 + (+0.0)` ever occurs. The filter is redundant with the zeroing.

**G6 removes the zeroing instead, and is detected.**

### 4. 001I G7n — continuity gathered in face-id order rather than `cell.faceIds()` order

*Category: the two sequences are identical.*

Floating-point addition is not associative, so traversal order is normally load-bearing. Here the
two orders are the **same sequence**: `cell.faceIds()` is already ascending for every mesh
generator in the repository. Verified by direct measurement rather than by reading the generators —
**0 cells with unsorted face ids** on `cartesian2d 8`, `cartesian3d 4` and `distorted q16`.

**G7 reverses the traversal instead, and is detected.**

The device still keeps both `cellFaceIds` and `sortedFaces` even though they currently coincide,
because the CPU semantics genuinely differ — continuity uses `cell.faceIds()`, the explicit-term
RHS pass uses face-id order — and a future generator emitting unsorted face ids would separate
them. On that day G7n stops being null.

### 5. 001J H9 — the 2×2 solve's factors reordered *within* each product

*Category: equivalent operand ordering under IEEE arithmetic.*

IEEE-754 multiplication is **commutative**: `Sxx*by` and `by*Sxx` produce bitwise identical results,
including for signed zeros, infinities and NaN payload-free NaNs. The mutation reorders operands
*within* products only; the operands of the subtraction are unchanged, and addition/subtraction
order is what would matter. So nothing can change.

This one is worth flagging because the kernel originally carried a comment claiming the opposite —
*"making them symmetric changes the rounding"* — which is false. The comment was corrected, and
**H9b** and **H9c** were added as the real packing controls: they change *which* value is read, not
the order of a product, and both are detected.

### 6. 001J H13 — a 2D cell's least-squares weight includes the z term

*Category: addition of exact zero, with the `-0.0` case eliminated.*

Every displacement of a 2D cell has `dz == 0.0`, so `dz*dz` is `+0.0`, and the weight becomes
`((dx*dx) + (dy*dy)) + 0.0`.

The k11 trap is explicitly checked here: the left operand is `(dx*dx) + (dy*dy)`, a sum of two
squares. A real square is never `-0.0` — `(-0.0)*(-0.0) = +0.0` — and `(+0.0) + (+0.0) = +0.0`, so
the left operand cannot be `-0.0`. Therefore `L + (+0.0) == L` exactly, for every value it can
take. Exactly equal, not approximately.

### 7. 001L L3 — `dV` fed where `dU` belongs

*Category: algebraic identity, from a property of the codebase.*

> **The momentum matrix is component-independent.** In both the diffusion and the convection
> boundary terms the component enters only the RHS, via `selectComponent(uB, component)`, while the
> diagonal contribution `builder.add(ownerId, ownerId, coefficient)` is added for every boundary
> face regardless of the condition's type (`MomentumEquation.cpp:136`, `:236`, `:298`). Interior
> faces share one mass flux, one geometry and one viscosity.

So `dU == dV == dW` **exactly, always**, and substituting one for another cannot change any result.

This was first assumed to be a coverage gap. A `symmetryDuct` case was added expecting a Symmetry
patch — which removes only the normal component — to separate the diagonals. **It does not**, for
the reason above. The case is kept because Symmetry is the one velocity condition no other case
exercises, but its rationale was corrected.

The 001L harness now **asserts the property** (layer `R`, 6/6) rather than requiring its negation.
That is the right shape: if a future change ever makes the diagonal component-dependent, the
assertion fires and L3 stops being null the same day.

**Consequence, stated rather than hidden:** the anisotropic response `D = diag(d_u, d_v, d_w)` that
`pressureCorrectionFaceCoupling` is written for is **isotropic in practice** in this codebase. The
operator is general; that is not a defect. But it means a whole class of component mis-wiring is
undetectable, and claiming otherwise would be claiming coverage this project does not have.

### 8. 001M M7 — which component's diagonal feeds which response coefficient

*Category: the same algebraic identity as L3.*

Carried forward rather than dropped, because the 001L harness asserts the underlying property on
every case.

---

## Unreachable under a valid production contract — 2

Both are in the least-squares gradient plan. Neither is "probably fine": each is backed by a
**measured count of zero** on every mesh in the gate, not by reading the code.

### 9. 001J H10 — the Green–Gauss fallback for an ill-conditioned cell dropped

The conditioning branch cannot be entered on any mesh `MeshGeometry` can build: every cell has
faces in at least two independent directions, so its displacement set is never colinear (2D) or
coplanar (3D), and the normal-equation matrix is never singular.

Observed directly: **`LS cells taking the GG fallback = 0` across all six meshes.**

The `isfinite` backstop is the only other route into the fallback, and a p' field at ~1e305 does not
overflow the weighted products on these meshes either — also checked.

The fallback is kept in production. It is correct code for a mesh this repository cannot currently
generate, and removing it because no test reaches it would be the wrong trade.

### 10. 001J H12 — the zero-distance displacement skip removed

No displacement on any mesh in the gate has zero length — two distinct cell centroids cannot
coincide — so the skip never fires. Observed directly: **`skippedEntries = 0` on every plan built.**

---

## Summary

| ID | gate | classification | proof category |
| --- | --- | --- | --- |
| NC1 | 001D convection | PROVABLY NULL | multiplication by exact zero |
| M6 | 001D convection | PROVABLY NULL | value written, never read in the active dimension |
| G6n | 001I pressure-correction | PROVABLY NULL | multiplication by exact zero |
| G7n | 001I pressure-correction | PROVABLY NULL | the two traversals are one sequence (measured) |
| H9 | 001J velocity correction | PROVABLY NULL | IEEE multiplication is commutative |
| H13 | 001J velocity correction | PROVABLY NULL | addition of exact `+0.0`, `-0.0` case eliminated |
| L3 | 001L single-iteration | PROVABLY NULL | algebraic identity (`dU == dV == dW`) |
| M7 | 001M integrated SIMPLE | PROVABLY NULL | the same identity |
| H10 | 001J velocity correction | UNREACHABLE | branch no valid generated mesh can enter (measured 0) |
| H12 | 001J velocity correction | UNREACHABLE | zero-length displacement cannot occur (measured 0) |

**None of these is counted as detected. None is counted as a failure.** Each is run on every
campaign, and a null control that is detected fails the run.
