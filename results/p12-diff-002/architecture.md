# P12-DIFF-002-A — matrix / stencil architecture

## 1. Question

The three-point reconstruction couples a boundary cell P to the far cell F across its opposite
interior face. F is generally **not** a face-neighbour of P, so DIFF-002-INV-001 flagged that the
coupling "may not correspond to an existing mesh face and therefore may not exist in the current
sparse matrix graph".

## 2. Answer: no new mechanism is required

CFDApp does not build matrices from a precomputed face graph. `algebra::SparseMatrixBuilder`
(`include/cfd/algebra/SparseMatrix.hpp`, `src/algebra/SparseMatrix.cpp:143`) accepts arbitrary
`(row, column, value)` triplets in any order and `build()`:

- `std::stable_sort`s by `(row, column)` — **deterministic sparsity**, independent of insertion
  order;
- sums duplicate `(row, column)` pairs — **no duplicate entries** (this is how `A(P,P) += …` already
  works);
- drops entries that sum to exactly zero, with no tolerance — so a coincidentally-zero far
  coefficient simply does not appear, deterministically.

All four assembly paths already add off-diagonal entries this way, e.g.
`MomentumEquation.cpp:151-154`:

```cpp
builder.add(ownerId, ownerId,     diffusionCoefficient);
builder.add(ownerId, neighborId, -diffusionCoefficient);
```

So the far-cell coupling is `builder.add(ownerId, farCellId, -farCoefficient)`. Nothing about the
mesh representation, the CSR layout, the SpMV, or the GPU CSR copy path needs to change, and there
is **no Cartesian-specific assumption** anywhere in it.

**Neighbour search cost.** F comes from `MeshGeometry::oppositeInteriorFace(mesh, cell, face)`, which
scans that one cell's own faces — O(faces per cell), already used by GRAD-002 on the same faces. No
global search, so no O(N²).

## 3. Solver compatibility — the one real risk, and why it is safe

A one-sided boundary stencil is **inherently asymmetric**: row P gains an entry in column F, while
row F gains no corresponding boundary term. The asymmetry is mathematically required by the
formulation, not accidental.

Every equation that calls `boundaryFaceDiffusionTerms` solves with **BiCGSTAB**, which does not
require symmetry:

| caller | solver | evidence |
| --- | --- | --- |
| `MomentumEquation.cpp:116`, `:213` | BiCGSTAB (case `momentum_linear_solver`) | `cases/*/solver.json` |
| `EnergyEquation.cpp:116`, `:196` | BiCGSTAB | `src/thermal/ThermalSolver.cpp:9,173` |
| `SpeciesEquation.cpp:77` | BiCGSTAB | `src/species/SpeciesSolver.cpp:9,143` |
| `KEpsilonEquation.cpp:116` | BiCGSTAB | `src/turbulence/KEpsilonEquation.cpp:233` |

The only symmetric solver, CG (`include/cfd/algebra/CG.hpp`: "Requires A to be symmetric"), is used
by the **pressure-correction** equation, which contains **zero** calls to
`boundaryFaceDiffusionTerms` (verified by grep). So the symmetric path is untouched.

## 4. Derived coefficients (the authoritative general form)

From DIFF-002-INV-001's geometry (`investigation/plan.md` §2): with n̂ the outward unit normal,
m̂ = −n̂, h1 = (x_f − x_P)·n̂, h2 = (x_f − x_F)·n̂, and the cell values transferred onto the
wall-normal ray by δ_P = x_P − (x_f + h1 m̂), δ_F = x_F − (x_f + h2 m̂):

```text
a = dphi/ds at the face = cP phi~_P - cF phi~_F - cB phi_b
    cP = h2 / (h1 (h2 - h1))
    cF = h1 / (h2 (h2 - h1))
    cB = 1/h1 + 1/h2
    phi~_P = phi_P - grad(phi)_P . delta_P,   phi~_F = phi_F - grad(phi)_F . delta_F
```

**Consistency identity** (a constant field must give a = 0), verified algebraically:

```text
cP - cF = (h2^2 - h1^2) / (h1 h2 (h2 - h1)) = (h1 + h2)/(h1 h2) = 1/h1 + 1/h2 = cB   ✓
```

**Uniform-grid limit** h1 = h/2, h2 = 3h/2: cP = 3/h, cF = 1/(3h), cB = 8/(3h), so
a = [9φ_P − φ_F − 8φ_b]/(3h) — the verification limit quoted in the authorization. The general form
above is what is implemented; the uniform formula is never hard-coded.

**Flux and assembly.** The diffusion contribution to the owner's balance is
`flux_into_owner = Γ |S| (∂φ/∂n) = −Γ |S| a`. CFDApp's assembly convention (derived from
`MomentumEquation.cpp:130-131`, where `A(P,P) += Df` and `rhs += Df·φ_b` for the two-point form) is
that the assembled row holds **−flux_into_owner**, with known parts moved to the RHS with a sign
flip. Therefore:

```text
A(P,P)  += Gamma |S| cP
A(P,F)  -= Gamma |S| cF
rhs[P]  += Gamma |S| cB phi_b                                  (boundary-value term)
rhs[P]  += Gamma |S| ( cP (grad_P . delta_P) - cF (grad_F . delta_F) )   (tangential transfer)
```

**The two-point form is the special case** cF = 0, cP = cB = 1/h1, for which
`Γ|S|/h1 ≡ Γ|S_orth|/|d|` — an identity DIFF-001 verified to 4.5e-16. So the fallback reproduces
today's coefficient exactly, by construction rather than by a separate code path.

**Supersession, not augmentation.** The new form computes ∂φ/∂n directly, so it replaces both the
implicit `|S_orth|/|d|` part *and* the explicit `S_nonorth·∇φ_P` part at value-prescribing faces.
The tangential information now enters through δ_P/δ_F only. Adding the old explicit term as well
would double-count it.

## 5. Interface change (minimal)

`boundaryFaceDiffusionTerms`'s **signature is unchanged**: the tangential transfer needs only
`gradPhi` (already passed) and geometry — the field values φ_P, φ_F are never needed, because they
are the unknowns and stay implicit. Only the returned struct grows:

```cpp
struct FaceDiffusionTerms {
  Real coefficient{};             // A(P,P)                     (unchanged meaning)
  Real explicitFlux{};            // rhs += this                (unchanged meaning)
  // P12-DIFF-002, boundary faces only; neutral values reproduce the two-point form exactly:
  Real boundaryValueCoefficient{};  // rhs += this * phi_b ; equals `coefficient` for two-point
  Real farCellCoefficient{};        // A(P, farCell) -= this ; 0 when the stencil is unavailable
  Index farCell{};                  // valid only when farCellCoefficient != 0
  bool higherOrder{false};          // true when the three-point stencil was used
};
```

Each call site gains two lines: use `boundaryValueCoefficient` for the φ_b term, and add the
far-cell entry when `higherOrder`.

## 6. Fallback

Deterministic and **topological**, never a floating-point threshold:

1. `oppositeInteriorFace` returns nothing (no interior face across the cell), or
2. the projections are unusable: `h1 > 0` and `h2 > h1` fail.

Condition 1 is pure topology (a face either is or is not a boundary face). Condition 2 can only fail
on a cell whose far centroid does not lie further from the wall than its own — geometrically
degenerate, and rejected by `MeshQuality` before any solve; it is guarded so it can never produce a
NaN or a sign flip. In the fallback the function returns exactly today's values, so one-cell-thick
meshes keep working and are **not** rejected.

Measured availability (`investigation/logs/04_topology.log`): **100 % of boundary faces on all 19
buildable committed cases**, including multi-block, graded, distorted and 3D; 0 % on 1×1 and
1×1×1, 11 % on 8×1 and 1×8, 20 % on 3D 8×8×1.

## 6a. The formula already exists in this codebase (found during DIFF-002-A)

`src/discretization/Diffusion.cpp`'s **explicit** diffusion operator has implemented a one-sided
boundary reconstruction since P0. Its 3-point branch (lines 165-173) is

```text
dphi/dn = a phi_b + b phi_P + c phi_N
  a = (2 h1 + H) / (h1 (h1 + H)),  b = -(h1 + H) / (h1 H),  c = h1 / (H (h1 + H))
```

with `h1 = |x_f − x_P|` and `H = ownerNeighborDistance(oppositeFace)`, i.e. the **P→F** distance.
Substituting `H = h2 − h1` into §4's coefficients:

```text
cB = 1/h1 + 1/h2 = (2 h1 + H)/(h1 (h1 + H))  = a      ✓
cP = h2/(h1 (h2 - h1)) = (h1 + H)/(h1 H)     = -b     ✓
cF = h1/(h2 (h2 - h1)) = h1/((h1 + H) H)     = c      ✓
```

**The formula derived independently by DIFF-002-INV-001 is the one already in the codebase**, and
∂φ/∂n = cB φ_b − cP φ_P + cF φ_F matches its sign convention. DIFF-002's contribution is therefore
(i) the non-orthogonal generalisation — normal projections plus the tangential transfer, which
`Diffusion.cpp` does not do — and (ii) carrying it into the **implicit** assembly path, where
momentum, thermal, species and k-ε actually live. This is a strong consistency check and is used by
W4: on an orthogonal mesh the new implicit coefficients must reproduce `Diffusion.cpp`'s 3-point
formula exactly.

**A subtlety that formula's own comment records, and which DIFF-002 must not overclaim.** Making
∂φ/∂n second-order *at the face* does **not** make the boundary cell's **Laplacian** second order
when h1 ≠ h2 (as it always is: h1 = h2/2 on a uniform grid). `Diffusion.cpp` therefore prefers a
**4-point cubic** path that back-computes the boundary flux so the cell's d²φ/dn² is second order,
guarded by `GridRefinementTest.LaplacianOfSmoothFieldConvergesAtSecondOrder`.

DIFF-002 deliberately does **not** mirror that 4-point path, for three reasons: it is a
Laplacian-targeted correction rather than a flux reconstruction ("changes only the boundary face's
own flux value, used by nobody but this cell" — untrue of an implicit assembly, where the value
enters the matrix); it would add a second extra matrix entry (P↔N2); and the known MESH-005 defect
("explicit `diffusion()` is wrong with exactly two cells along a boundary normal") lives in exactly
that machinery. This is consistent with DIFF-002-INV-001's measurements: the 3-point form improves
the *solution* error constant (4× on uniform spacing) without changing its order, which is precisely
what a first-order local truncation error confined to one ring of cells predicts.

Consequence for the "one implementation" requirement: the **3-point flux reconstruction** is
factored into `NonOrthogonalDiffusion` and used by both the implicit path and `Diffusion.cpp`'s
3-point branch. `Diffusion.cpp`'s 4-point path stays as a separate, documented, explicit-operator
Laplacian correction — a different mathematical object, not a second copy of the same formula.

## 7. Shared implementation

`NonOrthogonalDiffusion` stays the single authoritative implementation. `Diffusion.cpp:196-216`
currently repeats the same Dirichlet boundary decomposition independently; it is made to delegate to
`boundaryFaceDiffusionTerms` so the two cannot diverge. The six call sites (momentum ×2, thermal ×2,
species, k-ε) all consume the same struct.
