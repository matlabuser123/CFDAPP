# P12-GRAD-002 — Amendment A1 (pre-registered)

Authorized by the user on 2026-09-16, after the original GRAD-002 gate failed at C1 and the
post-failure investigation classified C1, C4 and C6 as gate-design defects
([a1/audit.md](a1/audit.md)).

**The original gate and its failure stand.** `acceptance_gate.md` (sha256 `a46973ed…`) and
`summary.md` are unchanged; A1 does not re-score the original run, and the original verdict
`P12-GRAD-002 BLOCKED / FAILED GATE` remains permanently recorded.

**A1 amends only the three criteria proven invalid** — C1 and C6 (invalid threshold derivation) and
C4 (invalid test domain, split into an acceptance criterion C4a and a diagnostic C4b). **C2, C3, C5,
C7–C14 are carried over verbatim** from the original gate and are not restated here except where
they reference C1/C4/C6, which they then read in their A1 form.

**Production source is not changed by A1.** The formulation under test is the one already in the tree
and already formatted (library sha256 `51e82ee8e0ed1636…`); A1 changes only what is measured and
against what. If fresh verification identifies an implementation defect, that is a failure, not a
licence to amend A1.

**Stop rule.** Stop at the first failed A1 criterion. Record `P12-GRAD-002 BLOCKED / FAILED GATE` and
`P12-MESH-007 BLOCKED / FAILED GATE`, and do not amend A1 after seeing a result.

**Procedural requirement, discharged before this document was frozen.** Every A1 test below was
dry-run against the unchanged pre-GRAD-002 baseline, and the result recorded in
[a1/logs/00_dryrun_baseline.log](a1/logs/00_dryrun_baseline.log) and summarized in
[a1/dryrun.md](a1/dryrun.md). Expected baseline outcome is pre-registered **per test** in §4: the
baseline must **pass** every ordinary floating-point/geometry sanity test (C1, C6, and the geometry
validity gate), and must **fail** only the tests that are negative controls for the old defect
(C4a's quadratic-field rows and C5).

## 1. The A1 floating-point envelope

All three replacements use one expression, evaluated per mesh and per field, so they cannot
contradict each other. For a measured absolute gradient discrepancy Δ (either an error against the
analytic gradient, or the difference between a mesh and its translated/rescaled copy):

```text
E  =  ε · K_face · Φ · G   +   K_geom · C_g · |∇φ|_ref
```

| symbol | meaning | how obtained |
| --- | --- | --- |
| ε | 2.220446049250313e-16 | machine epsilon |
| Φ | max \|φ\| over cells and boundary faces | the cancellation scale of the Green–Gauss sum |
| G | max over cells of `Σ_f \|S_f\| / V` (units 1/length; = 4/h in 2D, 6/h in 3D uniform) | measured from the mesh |
| C_g | the mesh's own geometry inaccuracy, as a fraction of h (definition per criterion below) | measured **independently of the gradient**, by the geometry checks of C4a |
| \|∇φ\|_ref | max analytic \|∇φ\| | the field |
| K_face | **8** | a face value `(1−w)φ_P + wφ_N` costs ≤ 3 ulp of \|φ\|, and `Σ_f\|S_f\|/V` converts that to a gradient; 8 covers 3 ulp plus the correction term's own roundings |
| K_geom | **100** | a cell-centred gradient is a divided difference over h, so a centroid inconsistency of δ·h perturbs it by O(δ)·\|∇φ\|; 100 allows two orders for stencil conditioning and the number of contributing faces |

Both constants are derived above and frozen here, **before** the fresh run. Neither is taken from a
GRAD-002 result. The first term is the `O(ε |φ| / h)` floor the authorization names; the second is
the coordinate-arithmetic term, and it is tied to a **measured** geometry inaccuracy rather than to
an assumed power law — the mistake C4 made was to assume that power (it used ε·X/h where the
shoelace centroid error is in fact ≈ 0.33 ε (X/h)³).

## 2. Replacement criteria

| id | A1 criterion |
| --- | --- |
| **C1-A1** | **Analytical consistency of constant and linear fields, within the floating-point envelope.** For φ ≡ 2.5 and for φ = a + bx + cy (+dz) with exact per-patch boundary conditions, on every **orthogonal, unskewed** valid-geometry gate mesh (2D Cartesian 16², 64², 256² plain / translated small / dyadic, L = 1e-3, L = 1e3, graded 1.2, and 3D 8³): the absolute error against the analytic gradient satisfies **Δ ≤ E**, with `C_g` = max over cells of \|computed centroid − exact centroid\| / h (known in closed form for every mesh in this list). **And**, as the authorization's "absence of an O(1) or discretization-scale artefact" requirement, the hard dimensionless bound **Δ · h / Φ ≤ 1e-9** must also hold on every mesh. <br><br>**Domain note, established by the pre-freeze dry-run** ([a1/dryrun.md](a1/dryrun.md)): the *distorted* mesh Q16 is deliberately **excluded from this envelope** and stays governed by the original, already-passing **C2** (≤ 1e-9 relative, all cells). On a skewed mesh the linear-field error is not floating-point round-off at all — it is the truncation residual of P12-NUM-003's four-sweep skewness-correction fixed point, whose established level (~1e-9) C2's bound was derived from. The baseline measures 1.725e-10 there against an envelope of 2.279e-11, i.e. applying a floating-point envelope to an iteration-limited quantity is an invalid test domain, the same class of mistake as the original C4. Q16's linear field is still reported, against C2's bound. |
| **C4a** | **Translation robustness inside the valid-geometry domain.** A mesh and its rigidly translated copy are admitted to this criterion **only if** independent geometry checks put the translated copy's geometry inconsistency — for cell volume (relative), cell centroid (/h), face centroid (/h), face-area vector (relative) — at or below **1e-6**, and its face closure `\|Σ_f S_f\| / Σ_f\|S_f\|` at or below **1e-14**. Rationale for 1e-6: an error a millionth of a cell size cannot be mistaken for any discretization effect, which is O(1) in these units; and the check is performed on the *translation difference* `x_B − offset − x_A`, which is measurable on any mesh without a closed-form geometry. Inside that domain, for the constant, linear and quadratic fields, **Δ ≤ E** with `C_g` = max over cells of \|x_B − offset − x_A\| / h. Meshes failing the geometry gate are **excluded from acceptance** and reported under C4b. |
| **C4b** | **Extreme-coordinate diagnostic — not an acceptance test.** For the large offset (1234.5678, 987.6543) at 16²–256², and for a coordinate sweep, record X/h, the geometry inconsistencies above, and the gradient difference, and report the X/h at which `createStructuredQuad2D`'s geometry first exceeds the 1e-6 validity tolerance. No pass/fail. The shoelace defect is **not fixed here**; it is recorded as a separately scoped pre-existing defect. |
| **C6-A1** | **Scale invariance under dimensionally correct normalization.** For the same physical mesh at L = 1e-3, 1 and 1e3 (each translated by 0.005 L, fields scaled correspondingly), every scale must satisfy **Δ ≤ E** — i.e. ρ ≡ Δ/E ≤ 1 at each scale — **and**, evaluated **per field across the three scales**, the ρ values excluding exact zeros must span no more than **100×** between largest and smallest. Rationale: every term of the formulation is a ratio of displacements, so ρ must be scale-independent up to floating-point conditioning; comparing raw Δ across scales, as the original C6 did, compares quantities whose floors differ by 10⁶. |

## 3. Carried over unchanged

C2, C3(a), C3(b), C5 (with the corrected self-checking instrument, rerun fresh), C7, C8, C9, C10,
C11, C12, C13, C14 — exactly as frozen in `acceptance_gate.md`. In particular:

- **C5's requirement and its 10 Δm + 1e-12 bound are untouched**, and its negative control must
  continue to demonstrate the old discontinuity.
- **C7's** static-cavity bound stays 1e-9, and **C8** still holds every previous phase to the
  threshold *that phase* froze.
- Because the new formulation intentionally changes genuinely non-orthogonal boundary gradients,
  C8/C11 do not demand bit identity where the method intentionally changed: every changed validation
  result must be identified, quantified, checked for convergence/conservation, and explained as a
  consequence of the corrected formulation. Unexplained regressions fail. No golden or reference
  output may be silently updated.

## 4. Pre-registered dry-run expectations (baseline = pre-MESH-007 library)

| test | baseline expected | why |
| --- | --- | --- |
| geometry validity gate | **PASS** on small/dyadic/scaled, **FAIL** beyond X/h ≈ 1e3 | a property of the mesh generator, identical in both libraries |
| C1-A1 (constant, linear) | **PASS** | the old branch is exact for linear fields too, so C1 is a pure floating-point sanity test |
| C2 row for Q16's linear field | **PASS** | governed by the original C2's 1e-9, not by the envelope (C1-A1's domain note) |
| C4a and C6-A1, constant and linear fields | **PASS** | as above |
| C4a and C6-A1, **quadratic** field, translated **Cartesian** meshes | **FAIL** | the negative control for the old defect: the baseline loses the second-order boundary treatment under translation (measured 8.065e-03 at 16²). Every criterion that translates a Cartesian mesh with a quadratic field is a negative control in this sense |
| C4a, quadratic field, **dyadic** offsets and **Q16** | **PASS** | exactly representable offsets keep the old exact predicate satisfied; on Q16 both meshes take the same (non-aligned) branch, so the defect does not manifest under translation there — consistent with MESH-007's Q16 result of 1.5e-13 |
| C6-A1 ρ-spread rows | **PASS** for constant and linear | the spread is a conditioning statement, not a defect test |
| C5 | **FAIL at the first step** | negative control for the old discontinuity |

A baseline failure anywhere else, or a baseline failure of a sanity test, invalidates that A1 test
and must be fixed **before** the freeze.

## 5. After A1

Only if every A1 criterion and all the carried-over verification pass: rerun the **original frozen
MESH-007 G6.3** unchanged — same Cartesian 16×16 translating lid cavity, same translation, physics,
step count and metric, and the same `max |u_B − b − u_A| ≤ 1e-8`, with no mesh substitution — then
G7.3, then the MESH-007 verification that stopped. If either fails, stop; MESH-007 stays blocked and
its original 2.49e-2 failure remains its only recorded G6.3 result.
