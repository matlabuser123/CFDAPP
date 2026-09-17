# P12-DIFF-002 A4-4 — independent derivations

No expected value here was obtained by running production and copying its output. Each is derived
from the documented DIFF-002 geometry (`results/p12-diff-002/architecture.md`), then compared with
what production produces as a *check*. Because A4 stopped at A4-2 (see `inventory.md` §9), **no test
was amended**; these derivations are recorded as the worked evidence that sub-class A is a mechanical
re-derivation, and as the starting point for whatever scope is authorized next.

## The formula being applied

Per value-prescribing boundary face, with `h1`, `h2` the wall-normal distances from the face to the
owner and to the far cell across the owner's opposite interior face:

```text
cP = h2 / (h1 (h2 - h1))      A(P,P)  += Gamma |S| cP
cF = h1 / (h2 (h2 - h1))      A(P,F)  -= Gamma |S| cF
cB = 1/h1 + 1/h2              rhs     += Gamma |S| cB phi_b
                              cP - cF  = cB   (identically)
```

With no valid inward stencil the historical two-point values stand: `cP = cB = 1/h1`, `cF = 0`, i.e.
`Gamma |S| / d`. On a uniform grid (`h1 = h/2`, `h2 = 3h/2`) the reconstruction reduces to
`[9 phi_P - phi_F - 8 phi_b] / (3h)`, used below only as an analytical cross-check.

---

## 1. `EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients`

Mesh `makeTwoCellMesh()` = `createCartesian2D(2, 1, 2.0, 1.0)` — two 1×1 cells on a 2×1 domain,
`k = 3`, unit depth. Every face area is 1; every boundary face sits 0.5 from its owner's centroid.

Cell A (left) has four faces: `left` (boundary), the internal face to B, `top` and `bottom`
(boundary). Only the `left` face has an opposite interior face (the internal one), so:

```text
left  face:  h1 = 0.5, h2 = 1.5  ->  cP = 1.5/(0.5*1.0) = 3      Gamma|S| = 3*1 = 3
                                     cF = 0.5/(1.5*1.0) = 1/3
                                     cB = 1/0.5 + 1/1.5 = 8/3     (check 3 - 1/3 = 8/3 OK)
             A(A,A) += 3*3   = 9      A(A,B) -= 3*(1/3) = 1       rhs += 3*(8/3) = 8 per unit T_b
top, bottom: 1 cell across y -> no opposite interior face -> FALLBACK
             A(A,A) += 3*1/0.5 = 6 each,  rhs += 6 per unit T_b each
internal:    k|S|/dPN = 3*1/1.0 = 3      A(A,A) += 3,  A(A,B) -= 3
```

```text
diagonal        = 9 + 6 + 6 + 3 = 24        (old: 3*6 + 3 = 21)
A(A,B)          = -(3 + 1)      = -4        (old: -3)
rhs[leftCell]   = 8*T_left + 6*T_top + 6*T_bottom
                = 8*10 + 6*15 + 6*5 = 200   (old: 6*10 + 6*15 + 6*5 = 180)
```

Production measures diagonal **24**, off-diagonal difference **1** (i.e. −4), and rhs **+20** over
180 → **200**. Derivation and measurement agree exactly. Uniform cross-check on the `left` face:
`[9 φ_P − φ_F − 8 φ_b]/(3h)` with `h = 1` gives coefficients 3, 1/3, 8/3 before the `Γ|S| = 3`
factor — the same numbers.

## 2. `SparseAssembly3DTest.TwoCellsInX`

`createCartesian3D(2, 1, 1, 1.0, 1.0, 1.0)` — two 0.5×1×1 cells; from the suite's own recorded
decomposition the diffusivity is `k = 2` (internal x-face `k·A/dPN = 2·1/0.5 = 4`, xmin boundary
`2·1/0.25 = 8`, each of the four y/z boundary faces `2·0.5/0.5 = 2`).

```text
xmin face (valid stencil, far cell = cell 1): h1 = 0.25, h2 = 0.75
    cP = 0.75/(0.25*0.5) = 6      Gamma|S| = 2*1 = 2   -> A(0,0) += 12   (old 8)
    cF = 0.25/(0.75*0.5) = 2/3                          -> A(0,1) -= 4/3
    cB = 1/0.25 + 1/0.75 = 16/3                         -> rhs  += 32/3
y/z faces: 1 cell across y and z -> FALLBACK, 2 each -> 8 total
internal x-face: 4
diagonal = 12 + 8 + 4 = 24                  (old 20)
A(0,1)   = -(4 + 4/3) = -16/3               (old -4)
```

Production measures **24** and **−5.333333333333333 = −16/3**. Agrees exactly.

The 2×2×1 and 2×2×2 variants follow the same derivation with their own cell sizes; production's
14 → **18** / −2 → **−8/3** and 9 → **12** / −1 → **−4/3** are the corresponding values, each the
old value plus `Γ|S|(cP − 1/h1)` on the one stencil-bearing direction.

## 3. `BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne` (this phase's own W4 test)

Its *baseline* block asserted that with the correction disabled the assembly is the two-point one:
`A(1,1) = 5`, `A(1,4) = −1`, `rhs(1) = 6`. On the 3×3 mesh of that test (h = 1, k = 1, `Γ|S| = 1`,
`h1 = 0.5`, `h2 = 1.5`) the DIFF-002 values are `cP = 3`, `cF = 1/3`, `cB = 8/3`, so with three
internal faces:

```text
A(1,1) = 3 (internal) + 3 (cP)        = 6      (old 3 + 2 = 5)
A(1,4) = -1 (internal) - 1/3 (cF)     = -4/3   (old -1)
rhs(1) = 8/3 * 3                      = 8      (old 2*3 = 6)
```

Production measures **6**, **−4/3**, **8** — i.e. the baseline now equals the corrected system,
which is exactly the activation invariance A2 was authorized to create. The assertion is obsolete;
the *corrected* half of the same test (6, 2/3, 16/3 for Γ = 2, already passing) is unaffected.

## 4. Sub-classes B–F

`ThermalBoundaryConsistency` needs no new constants: the remedy is the A3-2 pattern — evaluate the
boundary flux with the operator production uses
(`−(cP φ_P − cF φ_F) + cB φ_b + explicitFlux`) instead of
`boundaryFaceDiffusionTerms(..., nullptr, false).coefficient · (φ_P − φ_b)`.

Sub-class C needs a genuinely new reference: the "discrete exact" closed form
`G_discrete/G_exact = ny²/(ny²+2)` is the *old* scheme's own solution and has no DIFF-002 analogue
recorded yet; deriving the new discrete-exact channel solution for a three-point wall flux is new
work, not a constant swap. Sub-class D needs a numerical-policy decision on the order band's upper
limit. Sub-class F is not derivable as an instrument change at all (see `inventory.md` §6).

These are the reasons the remaining nine `AMEND_IN_A4` candidates are not treated as mechanical, and
are reported for a scope decision rather than silently amended.
