# P12-DIFF-001 — Second-Order Dirichlet Boundary Diffusion

Status: **BLOCKED / FAILED GATE**, stopped at workflow step 2 (the mandatory pre-freeze dry-run),
before any gate was frozen and before any production source was changed.

**The authorized root cause does not hold.** The Dirichlet boundary diffusive-flux operator does
**not** use a straight-line distance and is **not** first order *because of* non-orthogonality:

1. `boundaryFaceDiffusionTerms`'s coefficient is already **exactly Γ|S|/d_n**, the wall-normal form,
   verified to **4.5e-16** relative. Passing `|d|` as `distance` is algebraically cancelled by
   `|S_orth|`.
2. Its remaining first-order error is the textbook **half-cell one-sided difference**, measured to
   equal **0.5 h to five significant figures on a perfectly orthogonal Cartesian mesh**, with
   non-orthogonality at 48° adding only 0.1 %.
3. The authorized correction `φ_b ± ∇φ_P·d_t` changes the wall force by ≤ 0.05 % and leaves the
   observed order at **1.00**, so it cannot restore second-order flux convergence.

Stop rule 2 therefore applies and no production change was made. This also **corrects an
attribution error in my own P12-GRAD-002-INV-001 report**, recorded in §4 below.

## 1. What was run, and in what order

Steps 1 and 2 of the authorized workflow, both against the **unchanged** library
(`libcfdcore.a` sha256 `51e82ee8e0ed16363169b929934961c343ceff2eab58e656f1ada8ac0696f3cd`,
snapshotted to `$HOME/m7ref/grad002` so it stays available):

| log | probe | question |
| --- | --- | --- |
| [logs/01](logs/01_wallflux_dryrun.log) | `diff_wallflux.cpp` | reproduce the first-order wall flux, and dry-run the proposed correction in both signs, on orthogonal **and** distorted meshes, for constant / linear / parabolic fields |
| [logs/02](logs/02_identity.log) | `diff_identity.cpp` | is the production coefficient already Γ\|S\|/d_n, and how large is ∇φ_P·d_t? |
| [logs/03](logs/03_geometry.log) | `diff_geometry.cpp` | how large is the tangential offset on both failing cases' boundaries? |

Steps 3–15 were **not** reached. No gate was frozen (see `acceptance_gate.md`), no production source
was modified, no test, threshold or golden output was touched, and nothing was committed.

**A probe defect I caught and fixed before drawing any conclusion.** The first version of
`diff_wallflux` used `Γ∇φ·(−S)` as the exact reference and normalised by a vanishing exact total,
producing nonsense (relative errors of 1e+12). The production term is the diffusion contribution to
the owner's balance, `Γ∇φ·S_out`; with that sign and a non-vanishing physical scale
(`Γ|∇φ|A` summed over the wall) the probe reproduces INV-001's numbers exactly (6.2574e-02 at 64×8).

## 2. The measurements

### 2.1 The coefficient is already the normal-distance form ([logs/02](logs/02_identity.log))

```text
distortion 0.00 | |production coeff - Gamma|S|/d_n| / (Gamma|S|/d_n) = 2.776e-16
distortion 0.50 | ...                                                 = 3.997e-16
distortion 1.00 | ...                                                 = 4.488e-16
```

Algebraically: `S_orth = (S·S)/(d·S) d` ⟹ `|S_orth|/|d| = |S|²/(d·S)`, and `d·S = |S| d_n`, so
`|S_orth|/|d| = |S|/d_n`. The over-relaxed decomposition already removes the straight-line distance.
Consequently the whole expression is **algebraically exact for a linear field**, which the constant
and linear rows of [logs/01](logs/01_wallflux_dryrun.log) confirm (8.6e-18 for the constant field).

### 2.2 The first-order error is the half-cell error, present on orthogonal meshes

Total wall force against its analytic value, parabolic field, exact prescribed data:

| grid | 0.5 h | **orthogonal mesh (0°)** | distorted mesh (48°) |
| --- | --- | --- | --- |
| 64×8 | 6.2500e-02 | **6.2500e-02** | 6.2574e-02 |
| 96×12 | 4.1667e-02 | **4.1667e-02** | 4.1691e-02 |
| 144×18 | 2.7778e-02 | **2.7778e-02** | 2.7785e-02 |
| observed order | 1 | **1.000 / 1.000** | 1.002 / 1.001 |

The error is 0.5 h to five significant figures on a mesh with zero non-orthogonality, and
non-orthogonality contributes 0.1 % of it. Analytic confirmation: for φ = 6y(1−y) with the first cell
centre at h/2, `(0 − φ_P)/(h/2) = −6 + 3h` against an exact `−6`, a relative error of exactly h/2.
This is the standard finite-volume wall treatment, whose *solution* accuracy remains second order —
not a defect introduced by non-orthogonality.

### 2.3 The proposed correction does not change the order ([logs/01](logs/01_wallflux_dryrun.log))

Three formulations measured side by side on the 48° mesh, parabolic field:

| formulation | 64×8 | 96×12 | 144×18 | order |
| --- | --- | --- | --- | --- |
| production (as shipped) | 6.2574e-02 | 4.1691e-02 | 2.7785e-02 | 1.002 / 1.001 |
| normal foot, `φ_b − ∇φ_P·d_t`, distance d_n | 6.2574e-02 | 4.1691e-02 | 2.7785e-02 | 1.002 / 1.001 |
| stated sign, `φ_b + ∇φ_P·d_t`, distance d_n | 6.2489e-02 | 4.1663e-02 | 2.7777e-02 | 1.000 / 1.000 |

All three agree to four significant figures and all three are first order. The transfer term is
**not** small because the geometry is benign — `max |d_t|/d_n` is **0.724** on this mesh
([logs/03](logs/03_geometry.log)) — but because a fully developed profile's wall-cell gradient is
nearly wall-normal, so `∇φ_P·d_t` is ≤ **4.8e-04** of the wall jump, ~58× below the 2.78 % error.
The non-orthogonal explicit term as a whole is only 4.8e-04 of the implicit term
([logs/02](logs/02_identity.log)).

Tangential offsets on the other failing case are much smaller still: `max |d_t|/d_n` = 3.0e-02 and
1.4e-02 on the curved channel at its coarse and fine grids.

## 3. Sign convention, derived from the existing formulation as instructed

The production term approximates `Γ∇φ·S_out` as `coefficient (φ_b − φ_P) + Γ S_nonorth·∇φ_P`
(negative at a wall where φ decreases outward — confirmed against the measured values). With
`x_N = x_P + d_n n̂ = x_f − d_t`, transferring a **known** boundary value inward to the normal foot is

```text
phi_at_normal_foot = phi_b - grad(phi)_P . d_t     (minus)
```

The authorization's `+ d_t` is the correct sign for the *reverse* transfer, from the foot out to the
face centroid, which is what P12-MESH-001's oblique **Neumann** treatment needs and uses
(`boundaryValue(φ_P, d_n) + ∇φ_P·d_t`). Both were measured; neither changes the order.

## 4. Correction to my P12-GRAD-002-INV-001 report

INV-001's *measurements* stand: the wall flux is first order (6.2574 % / 4.1691 % / 2.7785 %,
order 1.00), identical in both libraries; the accuracy/convergence anti-correlation across
least-squares, old Green–Gauss and GRAD-002; the distortion sweep; and the cancellation evidence.

Its **attribution was wrong**: I wrote that the first-order error arises because
`MomentumEquation.cpp:112` passes the straight-line distance with no tangential correction. §2.1
shows `|S_orth|` cancels `|d|` exactly, so the normal distance is already in use, and §2.2 shows the
error is the half-cell error that an orthogonal mesh has in equal measure. I did not test the
orthogonal mesh in INV-001, and that omission produced the wrong mechanism. The *cancellation*
conclusion is unaffected — the wall flux is still the first-order partner whose error the old
gradient was compensating — but the partner's first-order character is **inherent to the two-point
wall difference**, not a non-orthogonality bug. Per the authorization's instruction not to modify
previous evidence, `results/p12-grad-002/investigation/summary.md` is left unchanged and this section
is the correction of record.

## 5. State

GRAD-002 is intact and untouched (step 12): `src/discretization/Gradient.cpp`
`d24882a96e5cbc00…`, `src/mesh/MeshGeometry.cpp` `0173e7e792b7e81b…`,
`include/cfd/mesh/MeshGeometry.hpp` `30034b62dbf79fe6…`, `libcfdcore.a` `51e82ee8e0ed1636…` — all
identical to the values frozen in A1 and in the investigation. No test, gate, golden output or prior
evidence was modified. The known P12-MESH-004 sanitizer defect
(`MeshQualityReport.DisconnectedMeshIsFatal`, use-after-free in the test code at
`tests/unit/mesh/test_mesh_quality_report.cpp:487-489`) is untouched and remains a pre-push CI
blocker; no sanitizer run was performed this phase.

## 6. Options

The wall flux genuinely is first order, and GRAD-002 genuinely exposed it. Making it second order is
a real change, but not the one authorized:

- **(a) A higher-order Dirichlet wall reconstruction** — e.g. a one-sided quadratic fit through the
  boundary value, the owner and the opposite neighbour (structurally what GRAD-002 does for the
  *gradient*), used for the wall flux. This changes the assembled coefficient structure for every
  value-prescribing face in momentum, thermal and species, so it is **substantial new scope** and
  needs its own phase; stop rule 6 covers it.
- **(b) Accept the exposure and amend the two production gates.** The *solution* order is unaffected
  (MESH-001 velocity order 2.086 / 1.736; MESH-003 velocity 1.919 / 1.975 and dp/dθ 1.827 / 1.898),
  the affected metrics are dp/dx order, a GCI consistency check and a rise-monotonicity check, and
  INV-001 showed the dp/dx metric's own scatter across distortion levels (0.5846–0.6135 % on the
  baseline alone) is the size of the change. Post-result gate amendments need explicit
  authorization.
- **(c) Leave GRAD-002, MESH-007 and the two cases as they are.**

One more piece of evidence would sharpen the choice between (a) and (b) and is cheap: measure
whether a one-sided quadratic wall flux is actually second order on these meshes, as a probe only,
before anyone commits to scope. That was not authorized here and was not run.
