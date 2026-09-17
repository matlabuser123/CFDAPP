# P12-GRAD-001 — Green–Gauss boundary-gradient predicate: acceptance gate (pre-registered)

Separately scoped numerical-correctness fix, authorized by the user on 2026-09-16 after the
P12-MESH-007 G6.3 failure. Frozen (sha256 in [logs/03_gate_freeze.log](logs/03_gate_freeze.log))
**before any change to `src/discretization/Gradient.cpp`**.

**Scope.** The geometric predicate that selects the Green–Gauss boundary treatment, and nothing else.
No ALE code changes. P12-MESH-007's G6.3 is **not** amended: its threshold stays
`max |u_B − b − u_A| ≤ 1e-8`, and its original failure stays on record
(`results/p12-mesh-007/summary.md` §5, §6).

**Stop rules.** Stop at the first failed item. Never weaken a threshold after seeing a result. If any
item fails, P12-MESH-007 stays BLOCKED / FAILED GATE and G6.3 is untouched.

## 1. The defect and its reproducer

**Reproducer** (no ALE, no MESH-007 code; `results/p12-mesh-007/logs/15`, reproduced on the
pre-MESH-007 BASE library and on NEW with identical output):

- a Cartesian lid-driven cavity, 16×16 on [0,1]², and a **rigidly translated copy of the identical
  mesh** (offset (0.005, 0.0025), built by the pre-existing `createStructuredQuad2D`);
- identical physics, boundary conditions, solver settings, time step and step count;
- static PISO, 20 steps: max |u_translated − u_original| = **5.418e-3 after step 1** and **2.487e-2
  after step 20**.

The physical geometry is identical up to translation, so the discrete solution should be too.

**Cause.** `tryPairedBoundaryContribution` (`src/discretization/Gradient.cpp`) gives a boundary cell a
second-order treatment (a quadratic fit through boundary, owner and opposite neighbour) only when

```text
cross(d, S_f) == Vector3{}     // d = x_f - x_P
```

holds **exactly in floating point**. That is an exact geometric predicate applied to computed
geometry. A translated Cartesian mesh satisfies it only to round-off, so 54 of 64 boundary faces fall
back to plain Green–Gauss, an O(h) different boundary discretization.

**Measured round-off** ([logs/01](logs/01_misalignment_prefix.log)), for the normalized misalignment
m_f = |d × S_f| / (|d| |S_f|) = sin(angle):

| mesh | max m_f | m_f / (ε (X/h)³) |
| --- | --- | --- |
| 2D 8² translated | 6.22e-14 | 0.65 |
| 2D 16² translated | 2.70e-13 | 0.32 |
| 2D 64² translated | 1.95e-11 | 0.34 |
| 2D 256² translated | 1.24e-09 | 0.33 |
| 2D 16², L = 1e-3 | 2.88e-13 | 0.34 |
| 3D 8³–32³ moved | ≤ 5.12e-14 | ≤ 0.047 |
| genuine: Q16 distorted | 1.76e-01 | — |
| genuine: shear θ | 2θ | — |

So round-off misalignment scales as **m ≈ 0.33 ε (X/h)³** for 2D shoelace geometry (X = the largest
absolute coordinate of the two points, h = 2|d|), and far less in 3D. Exactly representable offsets
(0, 1, 100, and 5 at L = 1e3) stay exactly aligned. The second predicate of the paired treatment, the
boundary/opposite **area** match (fixed 1e-12 relative), is **not** affected: the mismatch is exactly
0 on every translated Cartesian mesh (both faces span identical coordinate ranges), and 3.6e-2 on
Q16, so it is left unchanged.

**Audit of every other exact-geometry predicate** ([logs/00](logs/00_predicate_audit.log)): each one
selects between formulations whose difference is itself proportional to the misalignment or skew
(`obliqueNeumannFace`, `MeshGeometry::decomposeAreaVector`, `ownerNeighborCrossing`, the
pressure-correction component shortcut, the skewness corrections in `Interpolation`, `Gradient` and
`VectorGradient`). Those branch switches change results by O(round-off) and are left unchanged. The
paired boundary contribution is the only discontinuous one.

## 2. Tolerance derivation (never from the G6.3 result)

A tolerance must (a) exceed the round-off misalignment measured above, (b) stay far below any
consequential non-orthogonality, and (c) be invariant when the whole mesh is rescaled.

Treating a face with misalignment m as aligned makes the one-dimensional quadratic fit's direction
off by m, so the boundary gradient's relative error is O(m). Requiring that error ≤ 1e-6 caps the
tolerance at 1e-6.

The gate therefore requires a **dimensionless** criterion on m_f (scale-invariant by construction),
whose effective tolerance:

- is at least 20× the measured round-off of §1 for every mesh in GR1 and GR2;
- never exceeds 1e-6;
- classifies no face with m_f ≥ 1e-5 as aligned (GR3).

## 3. Criteria

ε = 2.220446049250313e-16. "Aligned" means the paired boundary treatment applies. Relative gradient
differences are normalized by the analytic gradient's magnitude scale. Meshes: Cartesian 2D 16², 64²,
256² (built by `createStructuredQuad2D` from Cartesian vertices, plain and translated by
(0.005, 0.0025)), the same at L = 1e-3 and L = 1e3 (offset 0.005 L), 3D 8³ moved by the MESH-007
kernel by (0.005, 0.0025, 0.001), and the distorted 2D mesh Q16.

| id | criterion |
| --- | --- |
| GR1 | **Translation and scale invariance of the gradient.** With identical field values and identical boundary conditions on a mesh and on its translated copy, the Green–Gauss gradient agrees cell by cell (interior and boundary-adjacent) to ≤ **1e-9** relative, for the constant, linear and quadratic fields, on every mesh listed above (2D 16², 2D 64², L = 1e-3, L = 1e3, 3D 8³). Pre-fix ([logs/02](logs/02_gradient_probe_prefix.log)): the boundary difference of the quadratic field is 5.06, 20.2, 1.15, 0.0 and 3.38 respectively. Derivation: after the fix both meshes take the same branch and the residual is geometric round-off, measured ≤ 5e-13; 1e-9 keeps ≥ 2000× margin, and the pre-fix failure exceeds it by ≥ 1e9. |
| GR2 | **The Cartesian second-order treatment survives round-off.** Every boundary face of every mesh in GR1, and of the 2D 256² translated mesh (whose round-off misalignment is 1.24e-9), must be classified aligned. Checked directly by the predicate unit tests (GR6) and indirectly by GR1. |
| GR3 | **Genuine non-orthogonality is never called aligned.** No boundary face with m_f ≥ **1e-5** may be classified aligned: checked on meshes sheared so that m_f = 1e-5, 1e-4, 1e-3 and 1e-2, and on Q16 (m_f = 0.176). Faces with m_f < 1e-5 may be classified aligned; the resulting directional error is ≤ m_f and is reported. |
| GR4 | **Analytical gradients.** Relative error against the analytic gradient: constant and linear fields, with exact per-patch boundary values, ≤ **1e-11** on every Cartesian variant (2D exact, shoelace, translated, scaled; 3D exact and moved), all cells; ≤ **1e-9** on Q16, interior cells (its patch-wise boundary values cannot be exact, which is reported). Quadratic field, interior cells: ≤ **1e-11** on the Cartesian variants and ≤ **1e-2** on Q16 (its genuine discretization error, pre-fix 1.5e-3). Pre-fix values are in logs/02; none may degrade by more than 10×. |
| GR5 | **The static translated-cavity reproducer.** The §1 reproducer, rerun unchanged: max \|u_translated − u_original\| over 20 steps ≤ **1e-9** (pre-fix 2.487e-2). Derivation: the two geometries differ by ≤ 3e-13 relative at 16², and 20 implicit steps do not amplify that beyond a few hundred times; 1e-9 keeps ≥ 1000× margin and sits 10× below MESH-007 G6.3's own 1e-8. |
| GR6 | **Predicate unit behaviour** (deterministic tests): exact Cartesian alignment; translation by a non-dyadic offset, small (0.005) and large (1234.5678); uniform mesh scaling (1e-3, 1e3); a single vertex perturbed by one ulp; a clearly non-orthogonal face; near-but-genuine faces (m_f = 1e-5, 1e-4); 2D and 3D. Every round-off case must be aligned, every genuine case must not be. |
| GR7 | **Existing verification.** (a) The P12-NUM-002/003 and P12-MESH-001–006 suites pass, with exact counts. (b) Full regression: Release and Debug + GUI 100 %, exact counts; ASan + UBSan with CI's settings, whose only permitted failure is the known pre-existing P12-MESH-004 test defect. (c) Every committed case and CLI fixture: outputs compared against the pre-fix BASE — byte-identical, or the difference explained, bounded, and attributed to a boundary face whose classification changed. (d) The number of boundary faces whose classification changes is reported for every committed case mesh. |
| GR8 | **Bit-identity where nothing changes.** Any mesh whose boundary faces were already exactly aligned (every Cartesian, rectilinear and graded builder mesh) gives **bitwise identical** results to the pre-fix library: verified by the MESH-005 and MESH-006 bit-identity probes and by the CLI comparison. |

## 4. After the fix passes

Only then does P12-MESH-007 resume, by rerunning the **original frozen** G6.3 and G7.3 unchanged
(same mesh, steps, physics, metric and 1e-8 threshold), followed by the verification that stopped:
G2.3, G9, the performance baseline, documentation, clang-format, focused tests and G10.
