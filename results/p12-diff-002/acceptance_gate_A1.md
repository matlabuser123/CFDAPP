# P12-DIFF-002 — Amendment A1 acceptance gate (W3b only)

Authorized 2026-09-16, after the frozen P12-DIFF-002 run stopped at W3b with
`P12-DIFF-002 BLOCKED / FAILED GATE`.

**Scope: W3b only.** W1a, W1b, W1c, W1d, W2, W3a, W4 and W5–W10 are untouched, as are the GRAD-002
and MESH-007 gates. No unrelated threshold is weakened. The original frozen gate
(`acceptance_gate.md`, sha256 `51079f6da5dd0a417b39fe332f92eccf32a9877a96035168bc566ed1d3a9e46b`) is
**not deleted and not rewritten**, and the original W3b failure and all its evidence
(`logs/08_gate_W3b.log`, `logs/09_gate_W3b_patches.log`, `summary.md` §3) remain permanently
preserved.

**Chronology this amendment must preserve:**

```text
Original W3b — FAILED
Amendment A1 W3b — (result of the fresh run below)
```

---

## 1. The original W3b, verbatim

> **W3b** | Degenerate meshes (2D 1×1, 8×1, 1×8; 3D 1×1×1, 8×8×1) fall back on **every** face, and
> their results are **bitwise identical** to the pre-change library | 100 % fallback; bitwise identity

## 2. The original failure

Measured (`logs/08_gate_W3b.log`, `logs/09_gate_W3b_patches.log`):

| mesh | boundary faces | higher-order | fallback | bitwise identical | original W3b |
| --- | --- | --- | --- | --- | --- |
| 2D 1×1 | 4 | 0 | 4 | 4/4 | PASS |
| 2D 8×1 | 18 | 2 | 16 | 16/18 | **FAIL** |
| 2D 1×8 | 18 | 2 | 16 | 16/18 | **FAIL** |
| 3D 1×1×1 | 6 | 0 | 6 | 6/6 | PASS |
| 3D 8×8×1 | 160 | 32 | 128 | 128/160 | **FAIL** |

Per patch: 8×1 took the higher-order path on `left`/`right` only; 1×8 on `bottom`/`top` only;
8×8×1 on `xmin`/`xmax`/`ymin`/`ymax` (8 faces each) and never on `zmin`/`zmax`.

## 3. Why the original W3b was mis-derived

It required 100 % fallback on meshes that are one-cell-thick in **only one** direction. Across their
other direction(s) those meshes have 8 cells and therefore a genuine opposite interior cell, so the
higher-order stencil is legitimately available there. Requiring fallback would mean discarding an
available second-order stencil — the opposite of this phase's purpose — and it contradicts the
authorization's own rule, "fall back where no valid opposite interior cell exists", together with
"Do not reject otherwise valid one-cell-thick meshes merely because the higher-order stencil is
unavailable."

The only meshes in the original list that are thin in **every** direction are 2D 1×1 and 3D 1×1×1,
and both pass the original W3b completely, including bitwise identity on every face.

**Methodological lesson, recorded as required.** The original gate's pre-freeze dry-run (14 PASS /
8 FAIL against the unchanged two-point library, `logs/04_gate_dryrun_baseline.log`) gave W3b no
protection, because it passed there **vacuously**: the baseline library has no higher-order path at
all, so "100 % fallback" is tautologically true on it. *A baseline dry-run proves nothing about a
criterion the baseline satisfies vacuously* — a criterion asserting the absence of behaviour the
baseline cannot exhibit must additionally be checked against the question "can a correct post-change
implementation satisfy this at all?" For W3b the authorization itself already answered no. This
amendment's dry-run is therefore designed to be **two-sided** (§6).

## 4. W3b-A1 — the corrected criterion

For every tested boundary face, with the classification supplied by an **independent oracle**
(§5), never by the production implementation's own answer:

```text
if a valid opposite interior stencil exists:
    production must use the higher-order reconstruction
if no valid opposite interior stencil exists:
    production must use the existing two-point fallback
```

Acceptance, over the whole mesh set of §7:

| # | requirement | threshold |
| --- | --- | --- |
| A1-1 | classification agreement between production and the oracle | **100 %**, i.e. `classification mismatches = 0` |
| A1-2 | for every `FALLBACK_REQUIRED` face, the new contribution equals the pre-DIFF-002 contribution **bit for bit** (`coefficient`, `explicitFlux`, `boundaryValueCoefficient`, `farCellCoefficient`, compared as raw bit patterns) | `fallback bitwise mismatches = 0` |
| A1-3 | for every `HIGHER_ORDER_REQUIRED` face, the reconstruction is **actually exercised** and does not silently fall back: a non-zero far-cell coupling, terms that genuinely differ from the two-point ones, and the consistency identity `coefficient − farCellCoefficient == boundaryValueCoefficient` to ≤ 1e-12 relative | `higher-order-not-exercised = 0` |
| A1-4 | stencil availability is **topological**, not decided by a floating-point alignment predicate: the purely topological Oracle-T and the connectivity Oracle-G must agree with each other wherever both apply | `oracle T/G disagreements = 0` |
| A1-5 | the mesh set contains **both** classes, so 100 % agreement is falsifiable in both directions | `HIGHER_ORDER_REQUIRED > 0` and `FALLBACK_REQUIRED > 0` |

## 5. The independent topology oracle

`results/p12-diff-002/a1/tools/diff2_oracle.cpp`. Two oracles, built on deliberately different
information so that neither can be circular. Neither is defined as "whatever the production
implementation selected"; production's answer is read only afterwards, from
`FaceDiffusionTerms::higherOrder`, and compared.

**Oracle-T — analytic, purely topological, no floating point at all.** From the mesh generator's
documented topology only: a structured block created with `(nx, ny, nz)` names its boundary patches
by axis extreme (`left`/`right`/`xmin`/`xmax` → x, `bottom`/`top`/`ymin`/`ymax` → y,
`zmin`/`zmax` → z), so a patch normal to an axis has a second cell inward exactly when that axis has
**≥ 2 cells**:

```text
oracleT(patch, nx, ny, nz) = HIGHER_ORDER_REQUIRED  if cellsAcross(axis(patch)) >= 2
                             FALLBACK_REQUIRED      otherwise
```

It never reads a coordinate, a centroid, a normal or any production function — it is an integer
comparison on the generator's own arguments. Applies to every single-block structured mesh, including
graded and distorted ones (MESH-001/MESH-002 guarantee topology identical to `createCartesian2D`).

**Oracle-G — general connectivity walk, by normal DEPTH.** For boundary face `f` of cell `P`, walk
`P`'s own faces through the cell-adjacency graph; an interior face `g` of `P` leads to a cell `F`.
With `n_out` the outward area vector of `f` and `s(c) = (x_f − x_c) · n_out` the depth of a cell
along the wall normal:

```text
oracleG(f) = HIGHER_ORDER_REQUIRED  if some interior face of P leads to F with s(F) > s(P)
             FALLBACK_REQUIRED      otherwise
```

This is exactly the mathematical requirement (a second point strictly deeper along the normal,
`h2 > h1`). It selects by **depth**, whereas production's `MeshGeometry::oppositeInteriorFace`
selects the most **anti-parallel** interior face — a different rule, so agreement is evidence rather
than tautology. The ordering is invariant under positive scaling of `n_out`, so no normalization and
no division is performed. Applies to every mesh, including multi-block and curved, where Oracle-T's
structured patch naming does not.

Where both apply, Oracle-T is authoritative and the disagreement count against Oracle-G is reported
(A1-4). Note for the record: production's stencil availability currently does depend on
`oppositeInteriorFace`'s alignment test `alignment < 0`; A1-4 is the check that this never diverges
from topology on the tested set. It is not a near-zero threshold — the genuinely opposite face has
alignment ≈ −1 — but the divergence is measured, not assumed.

## 6. Pre-freeze dry-run — two-sided, non-vacuous

Run before this document was frozen, on the full §7 mesh set: **12 920 boundary faces**, of which the
oracle requires HIGHER_ORDER on **12 366** and FALLBACK on **554**. Both classes are therefore
present in quantity (A1-5), and the run was repeated in three modes
(`logs/02_dryrun_production.log`, `logs/02_dryrun_baseline.log`, `logs/02_dryrun_overeager.log`):

| mode | what it substitutes | class mismatches | verdict |
| --- | --- | --- | --- |
| `production` | nothing — the real classification | 0 | **PASS** |
| `baseline` | the pre-DIFF-002 library: fallback on every face | **12 366** | **FAIL**, as required |
| `overeager` | an implementation reconstructing on every face | **554** | **FAIL**, as required |

The `baseline` control is decisive: it is exactly the library state on which the **original** W3b
passed vacuously, and W3b-A1 rejects it. The `overeager` control shows the criterion also has power
in the other direction, so it cannot be satisfied by simply always reconstructing. Neither negative
control modifies production code; both substitute the classification a broken library would report.

## 7. Required meshes

Structured, both required sets in full:

```text
2D  1x1   8x1   1x8   8x8
3D  1x1x1  8x1x1  1x8x1  1x1x8  8x8x1  8x1x8  1x8x8  8x8x8
```

Graded, distorted, multi-block and curved:

```text
graded 2x4 geometric r=2            graded 16x16 geometric r=1.2
distorted 16x16 shear 0.20/0.45     distorted 8x1 and 1x8 shear 0.20
every buildable committed case under cases/ (multi-block, curved, 3D, graded, transpiration)
```

Distorted meshes displace **interior nodes only**, so the boundary patches stay exactly planar and
prescribed boundary values stay exact — the GRAD-002 instrument lesson.

Per mesh the fresh run reports: boundary faces; oracle HIGHER_ORDER_REQUIRED; oracle
FALLBACK_REQUIRED; production higher-order count; production fallback count; classification
mismatches; fallback bitwise mismatches; higher-order-not-exercised; oracle T/G disagreements.

## 8. Stop rule

A completely fresh run is performed **after** this document is frozen and hashed. If W3b-A1 fails:
**STOP** with `P12-DIFF-002 BLOCKED / FAILED A1 GATE`, do not continue to W5, and do not alter the
criterion again. If it passes, record `Original W3b: FAILED` / `Amendment A1 W3b: PASS` and resume
the frozen DIFF-002 sequence at **W5**, which is mandatory and must not be skipped.
