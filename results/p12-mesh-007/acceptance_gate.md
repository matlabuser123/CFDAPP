# P12-MESH-007 — Acceptance gate (pre-registered)

Written and frozen (sha256 in [logs/05_gate_freeze.log](logs/05_gate_freeze.log)):

- after the audit and the user's scope decisions ([architecture.md](architecture.md) §1);
- before any MESH-007 source change and before any gate run.

**Phase gate:** uniform flow must remain uniform under prescribed mesh motion, to numerical tolerance.

**Stop rules.** Stop at the first failed item. Never change a threshold, configuration or reference
after seeing a result. An amendment is allowed only before the run it concerns, and must be
disclosed. Failed runs are kept.

**Pre-freeze observations** (not gate runs; no MESH-007 code existed):

- [logs/03](logs/03_prefreeze_roundoff_NOT_gate.log): numpy evaluation, independent of CFDApp, of
  the geometry formulas on the meshes and motions below. The measured round-off is ≤ 0.005 (2D) and
  ≤ 1e-4 (3D) of the GCL bounds, and ≤ 0.11 of the mesh-velocity bound.
- [logs/04](logs/04_prefreeze_solver_feasibility_NOT_gate.log): the existing static PISO converges at
  all 20 steps of the G6.3 cavity with the solver settings below.

## Definitions

- **Round-off constants:**
  - ε = 2.220446049250313e-16;
  - X = the largest absolute vertex coordinate at the time levels involved;
  - h_c = a cell's longest edge; h_f = a face's longest diagonal; N = the number of cells.
- **Fluid and solver:**
  - ρ = 1, μ = 0.01;
  - PISO solvers are BiCGSTAB, CPU, no preconditioner, absolute tolerance 1e-15, relative 1e-12,
    at most 5000 momentum and 20000 pressure iterations;
  - reference cell 0.
- **Sign conventions:**
  - S_f points owner → neighbour, or out of the domain at a boundary;
  - δV_f > 0 when the face moves along S_f;
  - s_Pf = +1 if P owns f and −1 if P is its neighbour;
  - φ_m,f = δV_f/dt; F_rel,f = F_f − ρφ_m,f.
- **GCL residual** of cell P: r_P = V_P^{n+1} − V_P^n − Σ_f s_Pf δV_f.
- **ALE mass residual** of cell P: R_P = ρ(V_P^{n+1} − V_P^n)/dt + Σ_f s_Pf F_rel,f. F is the step's
  final corrected flux.

**Meshes.**

| id | mesh |
| --- | --- |
| C16 | Cartesian 16×16 on [0,1]² |
| G16 | graded 16×16 on [0,1]²: geometric ratio 1.2, clustered at both ends, both axes |
| Q16 | structured quad 16×16: vertices X + 0.03 sin(πX) sin(2πY) e_x + 0.03 sin(2πX) sin(πY) e_y (the distortion vanishes on the boundary) |
| MB2 | two blocks [0,0.5]×[0,1] and [0.5,1]×[0,1], 8×16 each, aligned interface, patches left/right/bottom/top |
| P32 | Cartesian 32×8 on [0,2]×[0,1] |
| H8 | Cartesian 8×8×8 on [0,1]³ |

**Motions.** X is the reference position and τ = t − t₀, with t₀ = 0.

| id | motion |
| --- | --- |
| STAT | x = X |
| TR2 | x = X + τ(0.3, −0.2) |
| EX2 | x = X + 0.5τ(X − c), c = (0.5, 0.5) |
| SH2 | x = X + τ(Y − 0.5)e_x |
| SN2 | x = X + 0.05 sin(ωτ) sin(πX) sin(πY)(1, 1), ω = 2π/0.4 |
| PS2 | x = X − 0.25τX e_x: the right end of P32 moves at −0.5, x = 0 is fixed |
| TC2 | translation (0.5, 0.25) |
| TCC | translation (0.4, 0) |
| TR3 | x = X + τ(0.3, −0.2, 0.1) |
| EX3 | x = X + 0.5τ(X − c), c = (0.5, 0.5, 0.5) |
| SH3 | x = X + τG(X − c), G = [[0, 0.5, 0.3], [0, 0, 0.4], [0, 0, 0]] |
| SN3 | x = X + 0.05 sin(ωτ) sin(πX) sin(πY) sin(πZ)(1, 0.5, −0.75) |

Unless stated otherwise, geometry runs use dt = 0.02 and 20 steps.

## G1 — Static identity

| id | criterion |
| --- | --- |
| G1.1 | STAT on C16, G16, Q16, MB2 and H8, 5 advances: every cell centroid and volume, face centroid and area vector, and grid vertex is **bitwise** unchanged. Every swept volume and vertex velocity is exactly 0.0, and `moved` is false. |
| G1.2 | AlePISO with STAT vs PISO, 10 steps, dt = 0.01, on three flows: (a) the C16 lid cavity (Walls, lid MovingWall(1, 0)); (b) a Q16 channel (Inlet (1, 0) left, Outlet right with pressure FixedValue(0), Walls top and bottom); (c) an MB2 channel (Inlet left, Outlet right, Symmetry top and bottom). At every step, velocity, pressure, flux, maxCFL, continuityResidual and massImbalance are **bitwise** identical. |
| G1.3 | The geometry kernel on unchanged vertices vs the builder. Q16 and MB2: **bitwise** identical. C16 and G16: \|ΔV\| ≤ 16εX², \|Δx_c\|·V ≤ 32εX³, \|Δx_f\| ≤ 8εX, \|ΔS_f\| ≤ 8εX. H8: \|ΔV\| ≤ 64εX·h_c², \|Δx_c\| ≤ 64εX, \|Δx_f\| ≤ 16εX, \|ΔS_f\| ≤ 16εX·h_f. |
| G1.4 | The ALE operators in the static limit are **bitwise** equal to the static operators: `aleImplicitEulerTimeDerivative` with V^n = V vs `implicitEulerTimeDerivative`; `relativeMassFlux(F, 0)` vs F; the ALE momentum assembly with zero motion vs `assembleTransientMomentumComponent`, both overloads, on C16, Q16 and H8 (all components). |

## G2 — Geometry after every advance

Runs: TR2, EX2, SH2 and SN2 on C16; SN2 on Q16, G16 and MB2; TR3, EX3, SH3 and SN3 on H8.

| id | criterion |
| --- | --- |
| G2.1 | Every cell is valid (2D strictly convex and counter-clockwise; 3D all 8 corner Jacobians > 0), and min V > 0. |
| G2.2 | For the affine motions, the kernel geometry equals the affine image of the reference geometry. Volumes: 2D ≤ 16εX², 3D ≤ 64εX·h_c². Cell centroids: 2D \|Δ\|·V ≤ 32εX³, 3D ≤ 64εX. Face centroids: 2D ≤ 8εX, 3D ≤ 16εX. Area vectors: 2D ≤ 8εX, 3D ≤ 16εX·h_f. Total volume = det(A)·V_ref within N × the volume bound. |
| G2.3 | For SN2 and SN3, the kernel geometry (dumped vertices and results) equals an independent Python implementation (tools/independent_geometry.py) within the G2.2 bounds. |
| G2.4 | Per-cell closure \|Σ_f s_Pf S_f\|: 2D ≤ 8εX, 3D ≤ 64εX·h_c. |
| G2.5 | Orientation: internal faces S_f·(x_N − x_P) > 0; boundary faces S_f·(x_f − x_P) > 0. |
| G2.6 | Total volume. SN2, SN3, SH2 and SH3: constant within N × the volume bound. EX2 and EX3: equal to det(A)·V_ref within the same bound. TR2 and TR3: unchanged within the same bound. |
| G2.7 | Topology is identical after every advance (exact): cell ids and face lists, face ids with owner and neighbour, patch names and face lists, and each block's nx, ny, nz, name and vertex count. |
| G2.8 | Invalid motion is rejected with `InvalidArgumentError`, and the mesh geometry, vertices and motion time are **bitwise** unchanged. The message names: (a) for SN2 with A = 0.4 on C16, the cell (i, j) and its defect; (b) for SN3 with A = 0.4 on H8, the cell (i, j, k) and the corner; (c) for the axial collapse x = X − 2.6τ(X − 0.5)e_x on C16 (every cell inverts at the first step with τ > 1/2.6), the cell and its defect. A uniform 2D contraction past zero is a point reflection, which preserves orientation, so it is deliberately not used. (d) A 2D mesh with a motion that has a z component is rejected. |

## G3 — Mesh velocity

Runs as G2.

| id | criterion |
| --- | --- |
| G3.1 | STAT: every vertex velocity and every swept volume is exactly 0.0. |
| G3.2 | TR2 and TR3: \|v − b\| ≤ 8εX/dt per component. |
| G3.3 | EX2, SH2, EX3 and SH3: \|v − (G(X − c) + b)\| ≤ 8εX/dt. |
| G3.4 | SN2 and SN3: \|v − [d(X, t^{n+1}) − d(X, t^n)]/dt\| ≤ 8εX/dt, with the exact displacement d evaluated in the test; and \|v − ḋ(X, t^{n+½})\| ≤ (dt²/24)·Aω³ + 8εX/dt. |

## G4 — Geometric Conservation Law

Runs: every G2 run, every flow run in G5–G8, and STAT.

| id | criterion |
| --- | --- |
| G4.1 | Every cell at every step: \|r_P\| ≤ 256εX² (2D), ≤ 1024εX·h_c² (3D). |
| G4.2 | Globally: \|ΣV^{n+1} − ΣV^n − Σ_{b∈boundary} δV_b\| ≤ N × the G4.1 bound. |
| G4.3 | Swept volumes equal an independent formula implemented in the test within the G4.1 bound. 2D: the signed shoelace area of the swept quadrilateral (p^n, p^{n+1}, q^{n+1}, q^n). 3D: the 3×3×3 Gauss volume of the swept trilinear region. |
| G4.4 | TR2 and TR3: \|V^{n+1} − V^n\| ≤ the G4.1 bound, i.e. volumes are unchanged. |

## G5 — Uniform-flow preservation (primary)

**Runs.** AlePISO, 2D, dt = 0.02, 20 steps:

- flow: u0 = (1.0, 0.5), p0 = 1.0; Inlet(u0) on every patch; pressure FixedGradient(0) on every
  patch;
- initial flux: `calculateMassFlux` on the initial mesh.

| run | mesh | motion |
| --- | --- | --- |
| U1 | C16 | SN2 |
| U2 | C16 | TR2 |
| U3 | C16 | EX2 |
| U4 | C16 | SH2 |
| U5 | Q16 | SN2 |
| U6 | MB2 | SN2 |
| U7 | G16 | EX2 |

| id | criterion (every run) |
| --- | --- |
| G5.1 | Over all steps and cells: \|u − u0\|/‖u0‖ ≤ 1e-9 and \|v − v0\|/‖u0‖ ≤ 1e-9, with ‖u0‖ = √1.25. |
| G5.2 | \|p − p0\|/(ρ‖u0‖²) ≤ 1e-8. |
| G5.3 | All 20 steps Converged and min V > 0. The motion is non-trivial: the maximum vertex displacement from the reference is ≥ 0.25/16. For U1, U3, U5, U6 and U7, the largest single-step relative cell-volume change is ≥ 1 %. For U4, the largest change of a cell corner angle is ≥ 5°. |
| G5.4 | Discrimination: U1 and U3 rerun with the static formulation on the same moving mesh (PISO, no ALE terms) give max \|u − u0\|/‖u0‖ ≥ 1e-5. |
| G5.5 | Recorded every step: the maximum deviations in u, v and p; the continuity residual and global imbalance; the per-cell and global GCL residuals; min and max V; and the relative-flux CFL. |

**Threshold derivation.**

- Every source of velocity error per step is bounded by the relative GCL round-off plus the previous
  step's continuity residual:
  - the relative GCL round-off is ≤ 256εX²/V_min ≈ 1.4e-11 per step for X ≤ 1.2 and V_min ≥ 1/512;
    logs/03 measured about 1e-13;
  - the continuity residual is ≤ 1e-12 relative, set by the solver settings.
- Over 20 steps the worst case is ≤ 3e-10, below 1e-9.
- The pressure responds as δp/(ρ‖u0‖²) ≈ (velocity error)/CFL, with CFL ≈ 0.36 here, which gives
  the 1e-8 bound.
- The static formulation's error is O(ΔV/V per step) ≈ 1e-2 for U1 and U3. The gate therefore
  discriminates by more than 10⁶.

## G6 — ALE transport

| id | criterion |
| --- | --- |
| G6.1 | Hand-derived cases, agreement ≤ 1e-14 relative. (a) A unit cell whose east edge moves +0.1 in dt = 0.5, with uniform fluid (1, 0): swept volumes, φ_m, F_rel and the assembled time plus convection row. (b) A 2×1 mesh whose internal face moves: the owner and neighbour coefficients, including a case where the motion reverses the upwind direction (fluid 0.2, face moving at 0.5, so F_rel < 0). |
| G6.2 | The static limit is **bitwise** (G1.4). |
| G6.3 | Galilean invariance, dt = 0.01, 20 steps. Run A: PISO on the C16 cavity (Walls, lid MovingWall(1, 0)). Run B: AlePISO on C16 with TC2, walls MovingWall(b) and lid MovingWall(b + (1, 0)), initial u = b, p = 0. At every step: max \|u_B − b − u_A\| ≤ 1e-8; max \|(p_B − p_B[0]) − (p_A − p_A[0])\| ≤ 1e-8; max \|F_rel,B − F_A\| ≤ 1e-8/16. |
| G6.4 | Discrimination: B with the static formulation on the translating mesh gives max \|u_B − b − u_A\| ≥ 1e-4 after 20 steps. |
| G6.5 | 3D, operator level, on H8 with SN3 and with EX3; one advance t = 0.1 → 0.12. Uniform u0 = (1, 0.5, −0.25). F^n = `calculateMassFlux` at t^n with Inlet(u0) on every patch. For each component U, V and W, the assembled ALE time plus convection system (`aleImplicitEulerTimeDerivative` plus `assembleConvectionContribution` with F_rel) has max_P \|b − A·u0\|_P / (ρV_P‖u0‖/dt) ≤ 1e-10. The static formulation gives ≥ 1e-4. |

**Derivation for G6.3.** Runs A and B solve the same discrete systems up to round-off in the
translated coordinates. Each solve is converged to relative 1e-12, which bounds the per-step
difference to about 1e-10 (a condition number of 10–100 times 1e-12). Twenty steps give ≤ 2e-9,
below 1e-8.

## G7 — Moving wall

| id | criterion |
| --- | --- |
| G7.1 | **Piston.** P32 with PS2, dt = 0.05, 20 steps. Right: MovingWall(−0.5, 0). Left: Outlet, pressure FixedValue(0). Top and bottom: Symmetry. Initial u = (−0.5, 0), p = 0. The exact solution is u = (−0.5, 0), p = 0. Every step: \|u + 0.5\|/0.5 ≤ 1e-9, \|v\|/0.5 ≤ 1e-9, \|p\|/(ρ·0.25) ≤ 1e-8. The outlet mass flux equals ρV_pH = 0.5 within 1e-9 relative. No boundary-motion refusal. |
| G7.2 | **Couette in a translating mesh.** C16 with TCC, dt = 0.02, 20 steps. Bottom: Wall. Top: MovingWall(1, 0). Left and right: Outlet. Pressure FixedGradient(0) on every patch. Initial u = (y_c, 0). Every step: \|u − y_c\| ≤ 1e-9, \|v\| ≤ 1e-9. Discrimination: giving the bottom wall the mesh velocity, MovingWall(0.4, 0), gives max \|u − y_c\| ≥ 1e-3. |
| G7.3 | Walls translating with prescribed MovingWall(b): the G6.3 run. |
| G7.4 | Refusals with `InvalidArgumentError` naming the patch, the mesh reverted **bitwise**: (a) EX2 on C16 with Wall on every patch; (b) EX2 with Symmetry on every patch; (c) PS2 with a piston MovingWall(−0.25, 0), whose normal velocity mismatches the mesh. (d) G7.2's tangential mismatch is accepted. |

## G8 — Conservation

Runs: every step of U1–U7, G6.3-B, G7.1, G7.2, and a STAT run of the C16 cavity.

| id | criterion |
| --- | --- |
| G8.1 | max_P \|R_P\| ≤ 1e-9·F_ref, where F_ref = ρ·U_ref·h_ref. U_ref is ‖u0‖ (G5), 1 (the cavity), 0.5 (the piston) or 1 (Couette); h_ref is 1/16 (1/16 for P32). |
| G8.2 | Global: \|ρ(ΣV^{n+1} − ΣV^n)/dt + Σ_b F_rel,b\| ≤ N·1e-9·F_ref. |
| G8.3 | In the STAT run, R_P is **bitwise** equal to the continuity residual Σ_f s_Pf F_f. |
| G8.4 | Domain volume against the analytical value within N·16εX²: U3 and U7 follow (1 + 0.5τ)²·V_ref; the piston follows (2 − 0.5t)·1; U1, U2, U4, U5, U6, G6.3-B and G7.2 stay constant. |

## G9 — Backward compatibility

BASE is the pre-MESH-007 working tree, `$HOME/m7ref/base`, built in Release; its `cfdapp` is
byte-identical to MESH-006's final binary (logs/02).

| id | criterion |
| --- | --- |
| G9.1 | **Bit identity**: probe programs compiled against BASE and against NEW must print **bitwise identical** output. They cover: static PISO for 10 steps on the C16 cavity, a G16 channel, a Q16 channel and an MB2 channel; a TransientSolver run of the C16 cavity (history included); the MESH-005 and MESH-006 probes (2D SIMPLE); 3D SIMPLE for 20 iterations on the 8³ lid cube; the ThermalSolver in 2D and 3D; and every builder's geometry for C16, G16, Q16, MB2 and H8. |
| G9.2 | **CLI**: for every committed case and every CLI fixture, 2D and 3D, BASE vs NEW: fields.csv, solution.vtk and residuals.csv byte-identical, metadata.json equal as parsed JSON, stdout identical, exit codes equal. |
| G9.3 | No existing case, fixture or test input is modified. |
| G9.4 | Regenerated tracked outputs differ from BASE only in timing fields, classified file by file. |
| G9.5 | The existing PISO, transient and restart suites and the MESH-001–006 suites pass by name. |

## G10 — Regression

| id | criterion |
| --- | --- |
| G10.1 | Focused MESH-007 suites and the dependent suites, staged, with exact passed, failed and disabled counts. |
| G10.2 | Full regression, Release and Debug + GUI (offscreen): 100 % pass, with exact counts. |
| G10.3 | ASan + UBSan with CI's settings (`--timeout 7200`, CI's ASAN and UBSAN options). The only failure allowed is the known pre-existing P12-MESH-004 test defect `MeshQualityReport.DisconnectedMeshIsFatal`, which is reported, not fixed. MESH-007 code must produce 0 sanitizer diagnostics. |
| G10.4 | clang-format-18 is clean over CI's scope, and new or changed files build with 0 warnings. |

## Performance (recorded after the gate; measurement only)

- Geometry update, swept volumes and GCL cost per step: 2D 128² and 256²; 3D 32³ and 64³.
- Static PISO vs AlePISO step time on a 2D 128² mesh.
- Release build, one thread, hardware recorded. No optimization.

## Amendment A1 (written after the freeze, before any gate run; the text above is unchanged)

Written on 2026-09-15 while implementing, before any gate run. At this point only the core library
had been compiled; no test, probe or gate run of MESH-007 code existed. It concerns **G1.4 only**.
No threshold, run, mesh or motion changes.

**Reason.** G1.4 as frozen cannot be carried out literally, for two reasons:

1. Both transient momentum-component assemblies are two-dimensional by design: the static one since
   MESH-006 (`requireTwoDimensional`) and the ALE one because AlePISO is 2D (architecture §1). On H8
   both throw, so "vs … on H8" compares two refusals.
2. "Both overloads": the ALE assembly exists only in the effective-viscosity form, which is the one
   PISO uses. The static constant-viscosity overload is not an ALE counterpart. For a uniform μ it can
   differ from the effective-viscosity overload by round-off even on a static mesh, because the
   latter interpolates μ linearly at internal faces (`interpolateInternalFace`). A bit-for-bit
   comparison against it would test the two static overloads against each other, not the ALE
   static limit.

**G1.4 as amended** (every check bitwise):

- `aleImplicitEulerTimeDerivative` with V^n = V equals `implicitEulerTimeDerivative`, on C16, Q16
  and H8.
- `relativeMassFlux(F, 0)` equals F, on C16, Q16 and H8.
- The ALE momentum assembly with zero motion (the convecting flux equal to F, V^n = V) equals the
  effective-viscosity overload of `assembleTransientMomentumComponent`, on C16 and Q16, for both
  components. On H8 both assemblies must refuse with `InvalidArgumentError`.
- H8, components U, V and W: `assembleConvectionContribution` with `relativeMassFlux(F, 0)` equals
  the one with F (matrix and right-hand side). Adding `aleImplicitEulerTimeDerivative` with V^n = V
  gives the same system as adding `implicitEulerTimeDerivative`. This is the 3D static limit of the
  operators the ALE momentum is built from.

## Amendment A2 (written after the freeze and A1, before any gate run; the text above is unchanged)

Written on 2026-09-15 while writing the G5 and G6 tests, before any gate run. No MESH-007 test had
been compiled or run. It concerns the **discrimination controls of G5.4 and G6.5** and the last
bullet of the G5 threshold derivation. No threshold, run, mesh or motion changes.

**Reason: a design error in the frozen text.** G5.4 and G6.5 use "the static formulation on the same
moving mesh" as the negative control, and the G5 derivation claims its uniform-flow error is
O(ΔV/V) ≈ 1e-2. That is wrong.

- The static formulation uses V^{n+1} at both time levels and convects with the absolute flux F^n.
  For a uniform u0 its momentum residual is u0·[0 + ΣF^n], i.e. u0 times the previous continuity
  residual. That is the same as the ALE formulation's residual (architecture §3.6).
- It therefore preserves uniform flow as well. A static formulation is self-consistent: it treats
  each moved mesh as if it were stationary. Uniform-flow preservation alone cannot tell it apart
  from ALE.
- What uniform flow does detect is a formulation whose time term and mesh flux are inconsistent
  with each other, which is a violation of the discrete GCL.
- The items that tell ALE apart from the static formulation are G6.3/G6.4 (Galilean invariance),
  G7.1 (the piston) and G7.2 (translating-frame Couette). They are unchanged.

**G5.4 as amended.** U1 and U3 are rerun with two GCL-inconsistent variants of the ALE step. They
are test-only and use the library-internal PISO step (`detail::solvePisoStep`):

- **N1:** V^n in the time term, but the absolute flux F^n as the convecting flux (mesh flux
  omitted);
- **N2:** the relative convecting flux F^n − ρφ_m, but V^{n+1} at both time levels.

Each must give max |u − u0|/‖u0‖ ≥ 1e-5. The static formulation (PISO on the moving mesh) is also
run. Its deviation is recorded, not gated; it is expected to preserve uniform flow.

**G6.5 as amended.** The discrimination uses the same two variants at the operator level:

- N1: `aleImplicitEulerTimeDerivative` with V^n, plus convection with F^n;
- N2: `implicitEulerTimeDerivative` (V^{n+1}), plus convection with F_rel.

Each must give a relative residual ≥ 1e-4. The static operator (`implicitEulerTimeDerivative` plus
convection with F^n) is recorded, not gated.

**G5 derivation, last bullet, corrected.** The O(ΔV/V per step) ≈ 1e-2 error belongs to the
GCL-inconsistent variants N1 and N2, not to the static formulation.
