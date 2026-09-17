# P12-GRAD-002 — derivation of a continuous Green–Gauss boundary treatment

Frozen with the acceptance gate, before any production source change.

## 1. Chronology that leads here

```text
MESH-007 G6.3 failure
        ↓
pre-existing Green–Gauss defect identified
        ↓
GRAD-001 authorized
        ↓
hard tolerance formulation attempted
        ↓
GR1 failed
        ↓
GR3/GR6 shown mutually incompatible
        ↓
GRAD-001 stopped
        ↓
GRAD-002 separately authorized
```

`results/p12-grad-001/` is preserved unchanged. GRAD-001 is recorded as **BLOCKED / FAILED GATE**
and is not re-scored here. Its central finding is the premise of this phase: a single hard alignment
tolerance cannot separate round-off-induced apparent misalignment of a physically Cartesian
translated mesh (m_f ≈ 0.33 ε (X/h)³, which reaches 7.9e-4 at X/h ≈ 2e4) from genuinely small
non-orthogonality (which must not be treated as aligned). No cutoff value resolves that, so the
cutoff itself must go.

## 2. What the existing formulation does, and why it jumps

`greenGaussSweep` computes ∇φ_P = (1/V_P) Σ_f φ_f S_f,out. For a boundary cell,
`tryPairedBoundaryContribution` *replaces* the contribution pair {boundary face B, opposite interior
face O} with a one-dimensional quadratic fit through (B, P, F):

```text
dφ/dn_in = a φ_B + b φ_P + c φ_F ,   a = -h2/(h1(h1+h2)), b = (h2-h1)/(h1 h2), c = h1/(h2(h1+h2))
sum     += n̂_out · (−dφ/dn_in) · V_P
```

and is guarded by three conditions:

1. an opposite interior face exists (topological);
2. `d = x_B − x_P` is parallel to `S_B` — originally *exactly*, i.e. `cross(d, S_B) == 0`; GRAD-001
   replaced this with a tolerance on m_f;
3. `|area(B) − area(O)| ≤ 1e-12 area(B)` (a geometric threshold).

**Why the pair replacement needs (2) and (3).** The replacement is only the correct "normal part" of
the Green–Gauss sum when both faces are parallel to n̂ and carry equal areas: only then does
`S_B φ_B + S_O φ_O = A n̂ (φ_O − φ_B)`, whose division by `V_P = A(h1+h2)` is the normal difference
the fit supplies. So the Cartesian assumption is welded into the *structure* of the treatment, which
is why it had to be gated by a geometric test.

**Why the gate produces an O(1) jump.** The two branches differ at leading order, not by a term
proportional to the misalignment. Plain Green–Gauss at a boundary mixes an exact boundary value with
an O(h²)-biased opposite-face value in one central-difference-shaped sum, and the two errors no
longer cancel: the boundary cell's normal-direction gradient degrades from second to **first** order.
Crossing the threshold therefore switches the gradient by O(h) — an O(1) relative change — however
small the geometric perturbation that caused the crossing.

**The diagnosis.** The defect is not the choice of cutoff. It is that a *structural* Cartesian
assumption is selected by a *geometric* test.

## 3. Design objective

Compute the boundary cell's gradient by a formulation that

- is a smooth (rational, bounded-denominator) function of the vertex coordinates, so that
  `m_f → m_f + δ` changes the result by O(δ);
- uses no exact-zero or near-zero geometric branch;
- is geometrically general (arbitrary non-orthogonal, unequal-area, graded, 2D and 3D);
- **recovers the established Cartesian second-order result** in the aligned limit;
- depends only on coordinate *differences*, so it is translation- and scale-robust by construction.

## 4. The selected formulation: consistent second-order face values

Green–Gauss is exact for the volume-averaged gradient whenever every face value is exact. The
boundary defect is therefore not in the *sum* but in the *face values*: at a boundary cell one face
value is exact and its opposite is O(h²)-biased, so the pairwise cancellation that gives the interior
its second order is broken. The fix is to remove that leading bias from the face value, not to
replace the sum.

For an interior face O of a boundary cell P with far cell F, linear interpolation along the
owner–neighbour line has the exact error

```text
φ(x_f') − φ_interp(x_f') = −½ w(1−w) L² ∂²φ/∂ξ²  + O(h³)
```

with L = |x_F − x_P|, ξ = (x_F − x_P)/L, and w the interpolation weight at the crossing point f'
(`MeshGeometry::ownerNeighborCrossing::t`; w(1−w) is symmetric, so which cell owns the face does not
matter). Derivation: for φ(s) = φ_0 + φ'_0 s + ½φ'' s², linear interpolation between s = 0 and s = L
evaluated at s gives φ_0 + φ'_0 s + ½φ'' s L, so exact − interpolated = ½φ''(s² − sL) =
−½ φ'' w(1−w) L² with s = wL.

The second directional derivative is estimated from the three points that the boundary cell actually
has along ξ: the far cell F, the owner P, and the point **B′** where the line through P along −ξ
meets the boundary face's plane:

```text
t*   = ((x_P − x_B) · n̂_B) / (ξ · n̂_B)                  > 0
x_B′ = x_P − t* ξ
φ_B′ = φ_B + g_P · (x_B′ − x_B)
∂²φ/∂ξ² ≈ 2 [ (φ_B′ − φ_P)/t* + (φ_F − φ_P)/L ] / (t* + L)
φ_O(for P's sum) = φ_O + e_O ,   e_O = −½ w(1−w) L² ∂²φ/∂ξ²
```

`φ_B` is the existing boundary face value (`faceValues[B]`, so Dirichlet, Neumann and MESH-001's
oblique-Neumann treatment are inherited unchanged), and `g_P` is the previous sweep's gradient at P —
the same lagged-correction mechanism `greenGaussGradient` already uses for skewness (NUM-003) and
oblique Neumann faces (MESH-001). The correction is **cell-local**: it belongs to P's reconstruction,
exactly as the pair replacement it supersedes was cell-local.

### 4.1 It recovers the Cartesian result exactly

Uniform Cartesian boundary cell, boundary at the cell's own face: h1 = t* = h/2, L = h, w = ½, and
x_B′ = x_B exactly (the tangential transfer term is identically zero), so

```text
∂²φ/∂ξ² = (4/3h²)(2φ_B − 3φ_P + φ_F)
e_O     = −(1/6)(2φ_B − 3φ_P + φ_F)
φ_O+e_O = φ_P + (1/3)φ_F − (1/3)φ_B
```

For φ = φ_B + a x + ½ b x² with B at 0, P at h/2, F at 3h/2, the corrected value at x = h is
exactly φ_B + a h + ½ b h², i.e. the exact face value, and the Green–Gauss normal difference
(φ_O+e_O − φ_B)/h = a + b h/2 is the **exact** derivative at P.

More strongly, the corrected treatment and the existing paired fit are the **same linear
functional**: both are linear in (φ_B, φ_P, φ_F), and both are exact on the three-dimensional space
{1, x, x²}; a linear functional on a three-dimensional space that is exact on all of it is unique.
The tangential components are untouched (the two faces parallel to the boundary normal are summed
normally in both formulations), so the whole gradient vector agrees. The established Cartesian
second-order accuracy is therefore retained *by construction*, not by measurement — and the
remaining difference is floating-point arithmetic order only, which C11 bounds.

### 4.2 Why it is continuous

Every quantity — t*, L, w, n̂_B, x_B′, the divided differences — is a rational function of the vertex
coordinates. The only denominators are

- `ξ · n̂_B`, bounded by cos(max boundary non-orthogonality) ≥ cos 70° ≈ 0.34 on any mesh
  `MeshQuality` accepts;
- L > 0 and t* > 0 on any non-degenerate cell;
- `d · S_f` inside `ownerNeighborCrossing`, already guarded and already required by NUM-003.

No comparison of a geometric quantity against a tolerance selects a formula. Differentiating the
chain gives, for a perturbation δ of the normalized misalignment: x_B′ moves O(δh), φ_B′ changes
O(|g|δh), ∂²φ/∂ξ² changes O(|g|δ/h), e_O changes O(|g|δh), and the gradient changes O(|g|δ) — i.e.
**relative gradient change = O(δ)**, with no threshold at which it is O(1).

Remaining discrete choices in the boundary path, and why each is admissible:

| choice | kind | why it cannot jump |
| --- | --- | --- |
| does an opposite interior face exist | topological | a face is or is not a boundary face; independent of coordinates |
| `oppositeInteriorFace`'s most-anti-parallel argmax | geometric argmax | separated by a large margin on any quadrilateral/hexahedral cell; a tie needs a degenerate cell. The margin is measured and reported on every gate mesh, and C5 sweeps through the region empirically |
| two boundary faces claiming the same opposite face | geometric argmax | resolved deterministically (strongest anti-parallel alignment, ties by lowest face id) and reported; unreachable on structured meshes |
| sweep trigger (`x_B′ ≠ x_B`) | exact-zero test with **identically zero effect** at its crossing | when the tangential offset is exactly zero the correction term is exactly zero, so both sides of the branch give bit-identical results. This is the definition of continuity, not an exception to it |

### 4.3 Scale and translation robustness

Every term is built from coordinate differences and from ratios of them, so a uniform scaling
multiplies numerator and denominator alike and a translation cancels exactly in exact arithmetic.
What remains in floating point is the round-off of the differences themselves, ~ε X absolute, i.e.
η ≡ ε X/h relative. Propagating η through §4.2's chain: t*, L carry relative error η; the divided
differences in ∂²φ/∂ξ² carry absolute error ~η|g|/h after the near-cancellation, so e_O carries
~η|g|h and the gradient ~η|g|. Hence

```text
relative gradient sensitivity to translation  ≈  C ε (X/h),  C = O(10)
```

**linear** in X/h, against GRAD-001's measured (X/h)⁴ floor — the structural improvement this phase
claims and C4 tests. The estimate C ≤ 50 (w(1−w) ≤ ¼, a handful of terms, the cancellation
bookkeeping) is used to set C4's frozen factor K = 200, a 4× margin.

## 5. Candidates considered and rejected

| candidate | verdict |
| --- | --- |
| **Tune the GRAD-001 cutoff** | Rejected by authorization and by GRAD-001's finding: no cutoff separates round-off from genuine small non-orthogonality (GR3/GR6 incompatible). |
| **Least-squares gradient at boundary cells** | Geometrically general and continuous, but a *linear* least-squares fit through the boundary point and interior neighbours is only first order for the boundary-normal derivative, so it **loses** the established Cartesian second-order accuracy. Rejected by §3's fourth requirement. |
| **Quadratic-augmented least-squares reconstruction** (∇φ and κ_nn as unknowns over the whole boundary-cell stencil) | General, continuous, and recovers second order, but replaces the boundary cell's entire reconstruction, changes every boundary cell on every mesh by O(h²) rather than by round-off, and is underdetermined for a full 2D quadratic (5 unknowns, 4 faces). Rejected as larger than necessary. |
| **Smooth blend between the paired fit and plain Green–Gauss** | Explicitly disfavoured by the authorization, and rightly: it hides the branch behind an interpolation of two *inconsistent* formulas, with no consistency or convergence justification. Rejected. |
| **Generalize the pair-replacement structure** (normal-projected h1, h2 plus tangential corrections) | Keeps the arithmetic, and would preserve bit-identity on aligned meshes, but the replacement is only valid for parallel, equal-area face pairs; generalizing it to unequal areas has no clean form. Rejected: it would re-introduce the equal-area threshold. |
| **Consistent second-order face values (§4)** | **Selected.** Smallest change in the existing architecture (one more lagged face-value correction beside NUM-003's and MESH-001's, and it *deletes* the pair-replacement machinery and both geometric predicates), geometrically general, continuous, and algebraically identical to the current functional in the aligned limit. |

## 6. Consequences that must be measured, not assumed

1. **No bit-identity.** On aligned Cartesian meshes the new arithmetic is algebraically equal but not
   bitwise equal, so boundary-cell gradients differ at round-off and committed case outputs differ at
   solver-tolerance level. Bit-identity cannot coexist with removing the branch; earlier phases'
   bit-identity probes are expected to change. C11 bounds this; every difference is reported.
2. **Interior cells** use an unchanged code path, so they must stay bitwise identical on meshes with
   no skewed faces; on skewed meshes they can differ at round-off only through the existing sweep
   coupling. C10.
3. **Genuinely non-orthogonal meshes change at O(h).** There the paired treatment never applied, so
   the boundary gradient was first order and now becomes second order. This is an accuracy
   *improvement*, but it moves every distorted-mesh result of MESH-001/003/004 and NUM-002/003. C8
   holds those to each phase's own originally frozen thresholds.
4. **`boundaryFaceAlignment` and `FaceAlignment` are deleted** from production code: nothing needs
   them once no branch exists. GRAD-001's evidence describes a formulation that no longer exists,
   and says so.
5. **`obliqueNeumannFace`'s `cross(d, sf) == 0` test is also removed.** Its general formula
   (normal distance + tangential transfer) reduces *bit-identically* to the special case when the
   tangential offset is exactly zero, so deleting the predicate changes nothing on aligned meshes
   and nothing on already-oblique faces, and removes the last exact-zero geometric branch from the
   boundary-gradient path. C12.
6. **Out of scope, documented:** the interior skewness trigger (`skewVector != 0`),
   `decomposeAreaVector`'s exact-parallel short-circuit, `ownerNeighborCrossing`'s degeneracy guard
   and the pressure-correction shortcut all switch between formulations whose difference is itself
   proportional to the geometric quantity tested, so their effect at the crossing is zero — the
   benign class established by GRAD-001's audit (`results/p12-grad-001/logs/00`). They are not
   touched.
