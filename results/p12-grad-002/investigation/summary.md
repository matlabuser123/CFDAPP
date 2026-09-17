# P12-GRAD-002-INV-001 — production accuracy regression: findings

**Root cause, in one sentence.** The Dirichlet (wall) boundary diffusive flux in
`MomentumEquation` is **first order** on a non-orthogonal mesh — it takes the value difference along
the straight owner→face-centroid line and divides by |d|, with no tangential correction — and the
pre-GRAD-002 Green–Gauss boundary gradient was **also** first-order biased at those same cells, so
the two errors partially cancelled in the assembled momentum balance. GRAD-002 makes the gradient
second order (20× more accurate at wall cells), which **removes the cancellation and exposes the
flux error**.

**Classification: C (the previous validation benefited from the old first-order treatment) with B
(another operator is inconsistent with the corrected gradient) as the mechanism.** GRAD-002's
reconstruction is **not** mathematically incorrect. Investigation only: no production source, test,
threshold, golden output or prior evidence was modified, and nothing was committed.

## 1. Reproduction (step 2) — exact, both libraries, same code path

`inv_poiseuille.cpp` and `inv_curved.cpp` replicate the two failing tests' own mesh generators,
developed regions, metrics and order/GCI arithmetic, and link against either library.

### MESH-001 distorted Poiseuille ([logs/01](logs/01_poiseuille_new.log), [logs/02](logs/02_poiseuille_base.log))

| grid | metric | pre-GRAD-002 | GRAD-002 |
| --- | --- | --- | --- |
| 64×8 | velocity L2 / L∞ | 1.7488e-02 / 2.8878e-02 | 1.7466e-02 / 2.9253e-02 |
| | dp/dx error | 2.1908 % | 2.2800 % |
| | iterations | 1071 | 1983 |
| 96×12 | velocity L2 / L∞ | 7.4225e-03 / 1.2468e-02 | 7.4975e-03 / 1.3987e-02 |
| | dp/dx error | 1.1633 % | 1.0957 % |
| | iterations | 673 | 2105 |
| 144×18 | velocity L2 / L∞ | 3.2779e-03 / **5.7132e-03** | 3.7082e-03 / **1.1248e-02** |
| | dp/dx error | **0.5846 %** | **0.6788 %** |
| | iterations | **682** | **2789** |
| | column-flow / mass | 3.37e-10 / 3.37e-10 | **3.80e-11 / 1.54e-12** |
| orders | velocity, dp/dx | 2.114, 1.561 / 2.016, 1.697 | 2.086, 1.807 / **1.736, 1.181** |
| GCI | true error ≤ GCI21 | 0.005846 ≤ 0.009381 PASS | 0.006788 > 0.002851 **FAIL** |

The reported changes are confirmed exactly (dp/dx 0.585 % → 0.679 %, fine-pair dp/dx order 1.697 →
1.181). **Two things the A1 report did not have:** the iteration count rises **4.1×**, and mass
conservation *improves* by 10–200×. The velocity/Cartesian ratio moves 1.047 → 1.185, which is
within the test's own per-grid gate of 1.5× — so no velocity gate fails; the failing assertions are
the dp/dx order and the GCI consistency check only.

### MESH-003 curved channel ([logs/13](logs/13_curved_new.log), [logs/14](logs/14_curved_base.log))

| grid | metric | pre-GRAD-002 | GRAD-002 |
| --- | --- | --- | --- |
| 8×20 ×3 | velocity L2 | 2.0416e-02 | 2.0417e-02 |
| | rise error | 1.0919e-02 | **4.6407e-03** (2.4× better) |
| 12×30 ×3 | rise error | 5.9427e-03 | **1.0031e-03** (5.9× better) |
| 18×45 ×3 | velocity L2 | 4.2280e-03 | 4.2096e-03 (better) |
| | G relative error | 6.8185e-03 | 6.9026e-03 |
| | rise error | 2.9569e-03 | 1.4654e-03 (**still 2× better than base**) |
| | max \|u_r\| (exact 0) | 1.506e-04 | 5.955e-04 |
| | iterations | 1155 | **3246** |
| orders | velocity / G / rise | 1.916, 1.968 / 1.913, 1.968 / 1.500, 1.722 | 1.919, 1.975 / 1.827, 1.898 / 3.778, **−0.935** |

Non-monotonicity confirmed (1.0031e-03 → 1.4654e-03). But GRAD-002's rise error is **smaller than
the baseline's at every grid**; only its *sequence* is non-monotone. Velocity and dp/dθ keep second
order. The spurious radial velocity, which must vanish, stops decreasing under refinement for
GRAD-002 (9.64e-4, 5.47e-4, 5.96e-4) while the baseline's decreases cleanly (1.13e-3, 3.12e-4,
1.51e-4) — the fine grid is also the one needing 3246 iterations.

## 2. The gradient itself is 20× better, not worse (steps 3–6)

`inv_operator.cpp` imposes the **exact** analytic fields on the 144×18 mesh. Both fields have
exactly representable per-patch boundary conditions there, so these are pure operator errors. The
probe re-implements both treatments and is validated against both libraries: α=0 reproduces the
pre-GRAD-002 library to **0.000e+00** and α=1 reproduces GRAD-002 to **5.9e-15**
([logs/04](logs/04_operator_new.log), [logs/05](logs/05_operator_base.log)).

**Step 5, the α blend** (φ_f(α) = (1−α)φ_f_old + α φ_f_new, applied to the boundary correction):

| α | velocity-gradient L∞ | L2 | wall-cell L∞ |
| --- | --- | --- | --- |
| 0.00 (pre-GRAD-002) | 9.6802e-02 | 2.8749e-02 | 9.6802e-02 |
| 0.25 | 7.2708e-02 | 2.1818e-02 | 7.2708e-02 |
| 0.50 | 4.8712e-02 | 1.5017e-02 | 4.8712e-02 |
| 0.75 | 2.5034e-02 | 8.6558e-03 | 2.5034e-02 |
| 1.00 (GRAD-002) | 1.2175e-02 | 4.9311e-03 | **4.9529e-03** |

Monotone in α, and **20× more accurate at the wall cells** at α=1. Every region improves (interior
9.3047e-03 → 9.3048e-03 unchanged; inlet/outlet 1.6122e-02 → 1.2175e-02). The old exact-zero
predicate accepted **0 of 324** boundary faces on this mesh, so "old" there is plain Green–Gauss.

For the *linear pressure* field the new treatment is 2.4× worse (9.372e-08 → 2.236e-07) but at a
negligible level — 1.9e-07 relative to |∇p| = 1.2 — and that residual is the four-sweep
oblique-Neumann fixed-point remainder, not an inconsistency.

**So a globally worse CFD observable coexists with a locally 20× better gradient.** That is the
question the rest of the investigation answers.

## 3. Where the difference does *not* enter (steps 7–8)

| experiment | result | conclusion |
| --- | --- | --- |
| non-orthogonal corrections = 0 ([logs/06](logs/06_nonorth_new_0.log)) | **both libraries diverge** (L2 8.5e+03 / 8.8e+04) | the correction is essential; the MESH-004 finding "the uncorrected recipe diverges on smooth non-orthogonal meshes" is reproduced, so this row cannot isolate anything |
| corrections = 1 vs 2 vs 3 ([logs/07](logs/07_nonorth_new_2.log)) | identical to 4–5 digits in **both** libraries | **not** a correction-count, double-correction or lagging interaction |
| wall diffusive flux with the exact field, production formula ([logs/08](logs/08_wallflux_new.log), [logs/09](logs/09_wallflux_base.log)) | total wall-force error **2.7785 %** (new) vs **2.7789 %** (base) at 144×18 | the wall flux is **not** where the difference enters — but it is **first order** (6.2574 %, 4.1691 %, 2.7785 %; observed order 1.00) and **identical in both libraries** |
| gradient sweep convergence ([logs/19](logs/19_sweeps_new.log), [logs/20](logs/20_sweeps_base.log)) | change from 4→8 sweeps: 1.1e-08 (new), 5.2e-09 (base) | the gradient **is** converged in the four sweeps production uses; not an unconverged-inner-iteration effect (this hypothesis was tested and refuted) |
| `gradient_scheme = least_squares` ([logs/11](logs/11_leastsq_new.log)) | the two libraries are **identical digit for digit** (L2 3.4487e-03, dp/dx 0.6214 %, 675 iterations) | GRAD-002's change is confined to the Green–Gauss path, as designed |

## 4. Where it does enter — the decisive evidence

### 4.1 The wall flux is first order and unchanged; the gradient's error used to cancel it

The wall diffusive flux is assembled as `coefficient·(φ_b − φ_P) + explicitFlux` with
`coefficient = μ|S_orth|/|d|`, and `MomentumEquation.cpp:112` passes
`distance = MeshGeometry::distance(owner.centroid(), face.centroid())` — the **straight-line**
distance, with **no tangential correction of the prescribed boundary value**. On a mesh whose grid
lines meet the wall at 48° that approximates the wall-normal derivative only to first order, which
is exactly what the measurement shows (order 1.00, 2.78 % at the finest grid). Both libraries carry
this identically; the explicit non-orthogonal part is negligible (3.9e-05 of a 9.33 total).

### 4.2 Accuracy and convergence are anti-correlated — three independent ways

| gradient | wall-cell gradient L∞ (exact field) | SIMPLE iterations | case velocity L2 | dp/dx error |
| --- | --- | --- | --- | --- |
| least squares | **1.0185e-01** (worst) | **675** | 3.4487e-03 | 0.6214 % |
| Green–Gauss, pre-GRAD-002 | 9.6802e-02 | **682** | **3.2779e-03** (best) | **0.5846 %** (best) |
| Green–Gauss, GRAD-002 | **4.9529e-03** (20× best) | **2789** | 3.7082e-03 | 0.6788 % |

Least squares is *even less* accurate at the wall than the old Green–Gauss for this field, and
converges just as fast. A defective new gradient cannot explain this ordering; a **removed
cancellation** does: the two gradients whose boundary error is O(1e-1) both sit close to the
first-order wall flux's own error and converge quickly, while the accurate gradient leaves that
error uncompensated.

### 4.3 The effect switches on with misalignment ([logs/21](logs/21_distort_new_1.0.log))

Scaling the mesh distortion (the only causal knob available without changing production source):

| distortion | max non-orthogonality | iterations, base → new | velocity L∞, base → new | dp/dx error, base → new |
| --- | --- | --- | --- | --- |
| 0.00 | 0.00° | 643 → 797 | 4.5730e-03 → 4.5732e-03 | 0.6135 % → 0.6135 % |
| 0.25 | 12.99° | 522 → 2158 | 4.8608e-03 → 6.7250e-03 | 0.6038 % → 0.6197 % |
| 0.50 | 25.56° | 551 → 2229 | 5.1357e-03 → 8.7216e-03 | 0.5945 % → 0.6159 % |
| 1.00 | 48.25° | 682 → 2789 | 5.7132e-03 → 1.1248e-02 | 0.5846 % → 0.6788 % |

At zero distortion the two libraries agree to 4–5 digits, as A1 predicted. The slowdown and the L∞
growth appear with the first non-zero misalignment — i.e. with the tangential-transfer term, the
only part of the new treatment that is exactly zero on an aligned mesh. **The dp/dx error is
essentially flat (0.58–0.68 %) in both libraries, and the baseline's own value spans
0.5846–0.6135 % across distortions — the size of the entire "regression".**

### 4.4 Deeper convergence makes it worse, not better ([logs/16](logs/16_trace_new.log))

At 20000 outer iterations (case tolerances lifted to 1e-8):

| | U residual at 20000 | velocity L2 | dp/dx error | converged? |
| --- | --- | --- | --- | --- |
| pre-GRAD-002 | **2.446e-07** | 3.2782e-03 (682-iteration value: 3.2779e-03 — stationary) | 0.5918 % | no, but stationary |
| GRAD-002 | 4.537e-06 (18× larger) | **4.5991e-03** (2789-iteration value: 3.7082e-03 — still drifting) | 0.7736 % | no |

Continuity sits at ~1.8e-12 from iteration 100 in both, so the binding criterion is the
**U-momentum residual**. The baseline reaches its fixed point in 682 iterations and stays; GRAD-002
converges ~18× more slowly and its solution keeps moving *away* from the exact solution. So at full
convergence the new scheme's velocity error on this case is genuinely ≈1.4× the old one's — this is
a real discretization difference, exposed by (not caused by) the slow convergence.

### 4.5 Refinement (step 10) ([logs/18](logs/18_refine_new.log))

A fourth level (216×27, r = 1.5 continued) **diverges in both libraries** at the case's 3000-iteration
cap — base far worse (L2 2.43e+01) than new (3.84e-01). The family cannot be extended without
changing the case's relaxation/iteration settings, which this investigation is not authorized to do.
Within the three usable levels the new formulation's velocity order is 2.086 / 1.736 and the
baseline's 2.114 / 2.016.

## 5. MESH-003: the same root cause through a fragile metric (step 9)

Velocity L2 and dp/dθ keep clean second order under GRAD-002 (1.919/1.975 and 1.827/1.898), interface
conservation is unchanged (mass imbalance 1.8e-11 vs the baseline's 1.1e-10 — better), and the rise
error is *smaller than the baseline's at all three grids*. What fails is **monotonicity** of a metric
that is a difference of two ≈0.83 quantities whose own reference radius moves with the grid, measured
on the grid that needs 3246 iterations and whose spurious radial velocity has stopped converging.
So MESH-003 shows the same convergence-rate mechanism (B/C), with the metric's cancellation
sensitivity (**D**) determining that it appears as non-monotonicity rather than as a larger error.

## 6. Deformed 3D (step 11) — characterized only, a separate defect

`inv_quadrature.cpp` integrates ∫φ n dS exactly over each bilinear face (4×4 Gauss; the integrand is
degree ≤2 per variable, and 2×2 vs 4×4 agree to 1.3e-14) and compares it with the single-point
representation φ(x_f)·S_f the discretization uses ([logs/17](logs/17_quadrature_new.log)):

| mesh | max face warp / √A | ∫n dS vs stored S_f | **per-cell gradient error** |
| --- | --- | --- | --- |
| 3D 8³ Cartesian (planar faces) | 0.000e+00 | 2.2e-16 | **4.991e-15** |
| 3D 8³ deformed | 6.228e-02 | 4.4e-16 | **1.242e-03** |
| 3D 16³ deformed | 3.144e-02 | 5.8e-16 | **3.273e-04** |
| 3D 32³ deformed | 1.576e-02 | 6.5e-16 | **8.291e-05** |

The stored area vector is exact (6.5e-16); the error is entirely that φ is evaluated at one point on
a non-planar face. It is exactly zero on planar faces and converges at **order 1.92 / 1.98** —
matching the 1.93 measured for A1's failing linear-field gradient error, and the same in the
pre-GRAD-002 library (2.028e-03 vs 2.029e-03 at 8³). **Confirmed as a separate MESH-005
geometry/operator limitation (classification E)**, independent of GRAD-002 and not fixed here. (The
per-face *relative* columns in the log are degenerate where ∫φ n dS ≈ 0 — φ has a zero surface
inside the box — which is why the per-cell figure, normalised by V|∇φ|, is the meaningful one.)

## 7. Answers

| question | answer |
| --- | --- |
| Is the GRAD-002 formulation mathematically incorrect? | **No.** 20× more accurate at wall cells, monotone in α, second order restored on translated and distorted meshes, interior unchanged, mass conservation improved 10–200×. |
| Is another operator inconsistent with the corrected gradient? | **Yes** — the Dirichlet boundary diffusive flux (`MomentumEquation.cpp:112`/`:208` passing the straight-line distance with no tangential correction of the prescribed value). First order, 2.78 % at 144×18, identical in both libraries. |
| Did the old implementation benefit from error cancellation? | **Yes**, shown three ways: the accuracy/convergence anti-correlation including least squares; the distortion sweep; and deeper convergence making the new result worse while the old one is stationary. |
| Same root cause for both cases? | **Yes** (MESH-003 additionally through a cancellation-sensitive metric). |
| MESH-001 classification | **C + B** |
| MESH-003 classification | **C + B**, with **D** shaping how it appears |
| Deformed 3D classification | **E** (separate MESH-005 limitation) |

## 8. Recommended smallest production fix (not implemented)

**Make the Dirichlet boundary diffusive flux second-order consistent with the corrected gradient** —
the exact analogue of what MESH-001 already did for oblique *Neumann* faces, applied to
value-prescribing faces: split d into its normal part d_n = d·n̂ and tangential rest d_t, use d_n as
the two-point distance, and transfer the prescribed boundary value along the face with the owner
gradient (φ_b → φ_b + ∇φ_P·d_t) in the same lagged loop the other corrections already use. This
touches `boundaryFaceDiffusionTerms` and its callers, **not** GRAD-002.

Expected effect, from §4.1: the wall-force error should become second order instead of 2.78 % at
144×18, which is ~4× larger than the dp/dx error the tests measure — so it is the dominant
remaining boundary error and the only one large enough to explain the observed behaviour.

**Tests that would fail before the fix and pass after it**

1. A new wall-flux consistency test — the total wall force against the analytic value must converge
   at second order; it currently measures order **1.00** (6.2574 %, 4.1691 %, 2.7785 %) on both
   libraries, so it fails today and is the direct regression test for the fix.
2. `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` — currently fails its dp/dx
   order (1.181 < 1.5) and GCI assertions with GRAD-002 in place.
3. `MultiBlockProductionCase.CurvedChannelGridConvergence` — currently fails rise monotonicity and
   the rise-within-uncertainty assertion.
4. `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` will need
   updating **regardless** of this fix: it asserts the distorted-mesh order is *below* 1.9 and
   GRAD-002 now measures 1.936/1.969/1.984, i.e. it pins the defect. That is a test amendment, not a
   code fix, and needs its own authorization.

A caveat stated plainly: items 2 and 3 are *expected* to pass after the fix because the exposed
error would be removed, but that is a prediction, not a measurement — the fix is not implemented and
has not been run.
