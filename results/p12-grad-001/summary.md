# P12-GRAD-001 — Green–Gauss boundary-gradient predicate

Status: **FAILED GATE**. Two pre-registered items fail: **GR1** (translation invariance, 2.414e-9
against 1e-9, 2D 64² quadratic field) and **GR6** (predicate behaviour at the large non-dyadic
translation, 10 of 64 boundary faces misclassified).

Neither failure is a defect in the implemented fix. Both are defects in **the gate I froze**:

- GR1's 1e-9 bound was derived from the wrong quantity — the misalignment round-off (≤ 5e-13), not
  the boundary gradient's response to it. The true floor is ≈ 0.6 ε (X/h)⁴, which at 64² is 2.7e-9,
  above the frozen bound (§4).
- GR6 and GR3 are **mutually unsatisfiable** at the large-translation mesh: round-off there produces
  m_f = 7.9e-4, so GR6 requires those faces aligned and GR3 forbids exactly that (§5).

Per the user's stop rule ("If the independently scoped gradient fix fails its frozen gate: STOP"),
no threshold was changed, the original MESH-007 G6.3 was **not** rerun, MESH-007 stays
**BLOCKED / FAILED GATE**, and G6.3 is untouched.

## 1. Chronology (not rewritten)

1. **2026-09-15** — P12-MESH-007's gate was frozen before implementation
   (`results/p12-mesh-007/acceptance_gate.md`, sha256 `275eb19a…`, amendments A1/A2 disclosed before
   any gate run).
2. **G6.3 FAILED as frozen**: Galilean invariance of the translating lid cavity,
   max |u_B − b − u_A| = **2.49e-2** against 1e-8. That failure stands on record
   (`results/p12-mesh-007/summary.md` §5, §6, §9) and is unmodified by this phase.
3. MESH-007 **stopped at the failure**. G2.3's independent check, G9, G10, the performance baseline
   and the documentation were never run.
4. The root cause was **reproduced without ALE**, on the pre-MESH-007 BASE library, with static PISO
   on a Cartesian cavity and a rigidly translated copy of the same mesh: 5.418e-3 after step 1,
   2.487e-2 after step 20 (`results/p12-mesh-007/logs/15`). The defect is pre-existing and has
   nothing to do with moving meshes.
5. **2026-09-16** — the user authorized this separate fix, with G6.3 explicitly not amended.
6. This gate (`acceptance_gate.md`) was frozen, sha256
   `2c45e4381bb18099117ce9a4e69cfcbac4b6b9fbb7f284833a052dfcdbeb9f91`
   ([logs/03](logs/03_gate_freeze.log)), **before** `src/discretization/Gradient.cpp` was touched.
   The tolerance was derived from the measured floating-point scaling law of §2, not from the G6.3
   result.
7. The fix was implemented and the library rebuilt (libcfdcore sha256 `4fa871b8175b5648`, 0
   warnings). It removes the defect's mechanism: the quadratic-field boundary difference drops from
   5.06 to 8.6e-12 (16²), 20.2 to 2.414e-9 (64²) and 3.38 to 4.6e-14 (3D 8³).
8. **GR1 failed** at one of its five meshes (2D 64², quadratic field). **GR6 failed** at the large
   non-dyadic translation. Work stopped. The original G6.3 was not rerun.

Items 2 and 4 are the permanent record of the original failure. No result in this phase was obtained
by relaxing any threshold.

## 2. The defect and the measured scaling law (pre-fix)

`tryPairedBoundaryContribution` applied a second-order boundary treatment only when
`cross(d, S_f) == Vector3{}` held **exactly** in floating point — an exact geometric predicate on
computed geometry. For the normalized misalignment m_f = |d × S_f| / (|d| |S_f|),
[logs/01](logs/01_misalignment_prefix.log) measured

```text
m ≈ 0.33 ε (X/h)³        (2D shoelace geometry; X = largest |coordinate|, h = 2|d|)
```

so a translated Cartesian mesh satisfies the predicate only to round-off, and 54 of 64 boundary
faces silently fell back to plain Green–Gauss — an O(h) different boundary discretization. An audit
([logs/00](logs/00_predicate_audit.log)) confirmed this is the **only** discontinuous exact-geometry
predicate in the discretization; every other one selects between formulations whose difference is
itself O(misalignment).

## 3. The fix (kept in place)

`MeshGeometry::boundaryFaceAlignment` (new, documented in `include/cfd/mesh/MeshGeometry.hpp`)
returns the misalignment sine, the tolerance applied to it, and the verdict. `Gradient.cpp` now asks
for that verdict instead of testing an exact cross product. The criterion is dimensionless, so it is
invariant under uniform mesh scaling, and its tolerance follows the §2 law with a 64× factor
(≥ 190× the measured round-off) capped at 1e-6, the cap being what GR3's 1e-5 limit and the O(m)
directional-error argument of the gate's §2 allow.

No ALE code was changed, no cavity or coordinate was special-cased, and exactly-aligned faces still
compare aligned, so every already-exact mesh keeps its previous branch.

## 4. GR1 — FAILED as frozen, and why the bound was wrong

[logs/04](logs/04_gradient_probe_postfix.log), post-fix, translated-vs-original, relative:

| mesh | constant | linear | quadratic (boundary) | GR1 ≤ 1e-9 |
| --- | --- | --- | --- | --- |
| 2D 16² translated | 1.42e-14 | 8.79e-14 | 8.625e-12 | pass |
| **2D 64² translated** | 6.07e-14 | 5.62e-12 | **2.414e-09** | **FAIL** |
| 2D 16², L = 1e-3 | 2.00e-11 | 3.39e-12 | 1.192e-11 | pass |
| 2D 16², L = 1e3 | 0.000e+00 | 0.000e+00 | 0.000e+00 | pass |
| 3D 8³ moved | 5.96e-15 | 6.11e-15 | 4.584e-14 | pass |

Pre-fix the same quadratic column was 5.06, 20.2, 1.15, 0.0 and 3.38, so the mechanism is removed by
a factor of ~10⁹ where it was broken. But the frozen bound is 1e-9 and the 64² result is 2.414e-9.

**The residual is an irreducible round-off floor, not a remaining defect.**
[logs/05](logs/05_roundoff_floor_postfix.log) runs the same comparison with two offsets — one
non-representable, one **dyadic** (1/128, 1/256), where every coordinate is exact:

| mesh | non-dyadic offset | dyadic offset | max m_f | faces not aligned |
| --- | --- | --- | --- | --- |
| 16² | 9.342e-12 | **0.000e+00** | 2.91e-13 | 0 |
| 32² | 1.596e-10 | **0.000e+00** | 2.34e-12 | 0 |
| 64² | 2.678e-09 | **0.000e+00** | 1.91e-11 | 0 |
| 128² | 3.205e-08 | **0.000e+00** | 1.53e-10 | 0 |

Exact geometry gives exactly identical gradients at every resolution, and every boundary face is
classified aligned in all eight runs. The non-dyadic floor follows

```text
|Δ∇φ| ≈ 0.6 ε (X/h)⁴        (0.63, 0.67, 0.70, 0.53 × ε (X/h)⁴ at 16², 32², 64², 128²)
```

one power of (X/h) above the misalignment law of §2, because the boundary quadratic fit divides by a
difference of distances and so amplifies the geometric round-off by a further (X/h).

**My derivation error.** GR1's stated derivation — "the residual is geometric round-off, measured
≤ 5e-13; 1e-9 keeps ≥ 2000× margin" — used the *misalignment* round-off (5e-13) as if it bounded
the *gradient* difference. It does not: the boundary fit amplifies it. A correctly derived bound
would have been ~20 × 0.6 ε (X/h)⁴, i.e. ≈ 5e-8 at 64² and ≈ 3e-10 at 16². The frozen 1e-9 sits
below the floor at 64² and above it at 16², which is exactly the pattern observed. I did not amend
it.

## 5. GR6 — FAILED as frozen, and a contradiction between GR6 and GR3

[logs/06](logs/06_predicate_behaviour_postfix.log), 19 deterministic geometries:

| case | result | GR6 |
| --- | --- | --- |
| 2D 16² exact Cartesian (both builders) | 64/64 aligned, m = 0 | pass |
| 2D 16² translated (0.005, 0.0025) | 64/64 aligned, m ≤ 2.9e-13 | pass |
| **2D 16² translated (1234.5678, 987.6543)** | **54/64 aligned**, m ≤ 7.86e-4 | **FAIL** |
| 2D 64² / 256² translated | 256/256, 1024/1024 aligned | pass |
| 2D 16² at L = 1e-3 and L = 1e3 | 64/64 aligned | pass |
| one interior / one boundary vertex moved 1 ulp | 64/64 aligned | pass |
| genuinely sheared, m = 1e-5, 1e-4, 1e-3, 1e-2 | 0/64 aligned | pass |
| distorted Q16 (m = 0.176) | 0/64 aligned | pass |
| 3D 8³ exact, moved small, moved large | 384/384 aligned | pass |
| 3D 8³ sinusoidally deformed (m = 0.189) | 0/384 aligned | pass |

The one failure is the **large** translation GR6 demands. It is not an implementation slip: at
X/h ≈ 1.98e4 the §2 law predicts m ≈ 5.7e-4 and the measurement is 7.86e-4. The geometry genuinely
is that misaligned in floating point.

That makes two frozen criteria contradictory:

- **GR6** requires every round-off case, including this one, to be aligned — so the tolerance must
  exceed 7.9e-4;
- **GR3** forbids aligning any face with m_f ≥ 1e-5 — so the tolerance must stay below 1e-5.

Round-off crosses GR3's 1e-5 limit at X/h ≈ 5.1e3 (from 0.33 ε (X/h)³ = 1e-5). Beyond that, "treat
round-off as aligned" and "never treat 1e-5 as aligned" cannot both hold. **No choice of tolerance
passes this gate as frozen.** I froze both criteria without checking them against each other; the
implementation honours GR3 (cap 1e-6, 10× below its limit) and therefore misses GR6.

This also bounds the fix's domain of validity honestly: it restores translation invariance for
coordinate-to-cell-size ratios up to ~5e3 (all five GR1 meshes, and any realistic mesh near the
origin), and not beyond.

## 6. GR4 — measured, no degradation

[logs/04](logs/04_gradient_probe_postfix.log), against analytic gradients. Constant and linear with
exact per-patch boundary values: ≤ 1.42e-14 (2D exact, shoelace), 8.907e-14 (2D 16² translated, all
cells), 5.620e-12 (2D 64² translated), 1.091e-10 (Q16, distorted), 3.757e-15 (3D). Quadratic,
interior cells: ≤ 2.11e-13 on the Cartesian variants, 1.472e-03 on Q16 (its genuine discretization
error; pre-fix 1.5e-3). Nothing degraded; the large "all cells" numbers for the quadratic field are
the probe's own limitation (a single patch-wise boundary value cannot represent a quadratic along a
patch) and are reported as such, not as gradient error.

## 7. Items not run

Stopping at the first failure means these frozen items were **NOT RUN**: GR5 (the static
translated-cavity reproducer rerun), GR7 (existing verification, full regression, sanitizers, CLI
and case-output comparison, classification-change counts) and GR8 (bit-identity). GR2 passed as
written (every GR1 mesh and 256² fully aligned) and GR3 passed, both via logs/06.

[logs/05](logs/05_roundoff_floor_postfix.log) and [logs/06](logs/06_predicate_behaviour_postfix.log)
were run **after** GR1 failed, to characterise the failure. They are diagnostics, not gate retries:
no threshold was changed and no failed item was re-scored.

## 8. State of the tree

The fix is left in place, unbuilt into any committed artefact and uncommitted:

- `include/cfd/mesh/MeshGeometry.hpp`, `src/mesh/MeshGeometry.cpp` — `FaceAlignment` and
  `boundaryFaceAlignment`;
- `src/discretization/Gradient.cpp` — the predicate call.

Nothing was committed or pushed. No later phase was started. The unrelated pre-existing
P12-MESH-004 sanitizer defect (`MeshQualityReport.DisconnectedMeshIsFatal`, use-after-free in
`tests/unit/mesh/test_mesh_quality_report.cpp:487-489`) is untouched and still a pre-push CI blocker.

## 9. Decision

**FAILED GATE.** MESH-007 stays **BLOCKED / FAILED GATE** and G6.3 is unmodified. How to proceed is
the user's decision; the options I can see, in the order I would recommend them:

- **(a) Re-derive GR1 and reconcile GR6 with GR3, as a disclosed amendment, then rerun this gate.**
  GR1's bound should follow the measured 0.6 ε (X/h)⁴ floor (≈ 5e-8 at 64² with a 20× margin), and
  GR6's large-translation case should either be dropped as incompatible with GR3 or replaced by a
  stated validity limit of X/h ≲ 5e3. Both changes are post-result, so they need explicit
  authorization; the failures above stay on record.
- **(b) Remove the discontinuity instead of moving it.** Any predicate leaves the discretization
  discontinuous in the geometry — a blended or misalignment-corrected boundary fit, continuous in
  m_f, removes the whole defect class and makes GR6/GR3 moot. This is a larger numerical change and
  needs its own phase.
- **(c) Leave the fix in place unverified, or revert it, and leave MESH-007 blocked.**
