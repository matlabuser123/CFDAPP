# P12-MESH-007 — Mesh-motion architecture

Written before any MESH-007 source change. It is frozen with the acceptance gate
([acceptance_gate.md](acceptance_gate.md)).

## 1. Scope decisions (user, 2026-09-15)

**Library-level ALE only.** The production case format, CLI and GUI run steady SIMPLE only:

- `SolverConfigParser` rejects `"type": "PISO"`, and `CaseValidationTest.UnsupportedSolverTypeIsRejected`
  pins that behaviour.
- The GUI Solver page says transient runs are "not available through case files yet".
- Transient PISO and `TransientSolver` exist only in the C++ API.

Mesh motion needs time stepping, so it goes into that existing transient API. The case format, CLI
and GUI are unchanged. A `mesh_motion` case schema waits until transient runs exist in the case
format.

**3D at the geometry, GCL and ALE-operator level.** PISO has been 2D-only since MESH-006
(`requireTwoDimensional`). 3D gets:

- mesh motion, geometry update, mesh velocity, swept volumes and the GCL;
- the shared ALE time-derivative and convection operators, verified directly.

The uniform-flow gate for momentum and pressure runs in 2D PISO only.

Out of scope: remeshing, topology change, cell insertion or deletion, sliding interfaces, overset and
unstructured meshes, FSI, GPU moving meshes, turbulence on moving meshes, and restart of a
moving-mesh run.

## 2. Audit of the existing architecture (what MESH-007 builds on)

- **`Mesh`** owns cells (id, centroid, volume, face-id list), faces (id, owner, optional neighbour,
  centroid, area vector S_f) and boundary patches (name, face ids).
  - Geometry is set once, in the `Cell`/`Face` constructors, and has no setters.
  - `structuredBlocks()` keeps the generating vertex grid(s). The comment says they are for export
    only, and `VTKWriter` writes cell polygons from them.
  - There is no face-to-vertex connectivity; the vertex grids are structured.
- **Builders** (`MeshGeometry.cpp`):
  - `createRectilinear2D` (Cartesian, graded) computes geometry from the axis spacing.
  - `createStructuredQuad2D` and `createMultiBlock2D` compute it from vertices with shared helpers:
    `quadCellGeometry` (shoelace), and `verticalEdgeFace` / `horizontalEdgeFace` (midpoint, and the
    edge rotated, negated on the left or bottom side).
  - `createCartesian3D` computes box geometry from the axis spacing.
- **Operators** read geometry from the `Mesh` on every call. Interpolation weights, distances,
  gradients, diffusion, convection, pressure correction and CFL all cache nothing.
- **The one geometry cache is `SSTModel::wallDistance_`**, computed once at construction. The ALE
  solver is therefore laminar only.
- **Transient path:**
  - `TransientSolver` owns the time loop and accepts a step only when it is Converged, finite and
    within the CFL limit. It stops at the first rejection.
  - `PISO` implements one step: implicit-Euler momentum predictor with the lagged convecting flux
    F^n, then two pressure corrections.
  - `implicitEulerTimeDerivative` gives aP = ρV/dt and b = aP·φ^n.
- **Convection** (`assembleConvectionContribution`) is conservative upwind. For a uniform field its
  row residual is φ·Σ_f(outward) F_f.
- **Restart** fingerprints the full geometry (`MeshFingerprint`), so a restart written on a moved
  mesh cannot be resumed on the reference mesh; it is refused, not silently wrong.

## 3. Design

### 3.1 Geometry levels and ownership

| quantity | where | lifetime |
| --- | --- | --- |
| topology (ids, owner/neighbour, cell face lists, patches, grid sizes) | `Mesh`, never modified | the mesh |
| **current geometry** (cell centroid/volume, face centroid/S_f, grid vertices) | `Mesh`, replaced by `Mesh::setGeometry` | until the next update |
| **reference geometry** X (vertex positions when motion starts) | `MeshMotion` (copy) | the motion object |
| **previous geometry** (full snapshot before the last update; V^n; x^n) | `MeshMotion` | until the next advance; used by `revert()` |
| **mesh displacement** x − X | derived by `MeshMotion` from current and reference vertices | — |
| **mesh velocity** (x^{n+1} − x^n)/dt per vertex; face volumetric mesh flux δV_f/dt | `MeshMotionStep` from the last `advance` | until the next advance |
| **geometry update** | `MeshMotion::advance` → the geometry kernel → `Mesh::setGeometry` | one call per time step |

- The `Mesh` is owned by the caller. `MeshMotion` holds a non-owning reference and must not outlive
  it.
- Solvers keep holding `const Mesh&` and always see the current geometry. No parallel mesh
  representation exists, and connectivity is never copied.
- The one piece of added topology is the vertex map described in §3.3. It is built once and owned by
  `MeshMotion`.

### 3.2 `Mesh::setGeometry` (the only mutation path)

`Mesh::geometry()` returns a snapshot, and `Mesh::setGeometry(snapshot)` replaces it:

- cell centroids and volumes;
- face centroids and area vectors;
- each structured block's vertex coordinates.

`setGeometry` checks that every size equals the mesh's and that every value is finite. It also
requires every cell volume to be positive and every face area positive (naming the offending cell
or face), and requires a 2D mesh to stay in the plane (z = 0 and S_f.z = 0). The dimension never
changes.

The geometry setters on `Cell` and `Face` are private, with `Mesh` as a friend. Topology members have
no setter at all, so topology is preserved by construction.

### 3.3 Structured topology map and the geometry kernel (`MeshGeometry`)

`MeshGeometry::structuredTopology(mesh)` is built once from the mesh's own cells, faces and grids.

**Welded vertices.** Vertices with bitwise-identical coordinates across blocks become one global
vertex. Multi-block interface vertices therefore always move together.

**Cells.** Each cell stores its corner vertex ids:

- 2D: 4 corners, counter-clockwise: (i,j), (i+1,j), (i+1,j+1), (i,j+1).
- 3D: 8 corners, in the order (0,0,0), (1,0,0), (1,1,0), (0,1,0), then the same four at k+1.

Cells map to blocks in cell-id order, block by block, which is the `Mesh` constructor's invariant.

**Faces.** Each face is identified as one side of its owner cell:

- 2D: the nearest of the owner's 4 edge midpoints; 3D: the nearest of its 6 face vertex averages.
- The orientation (the builder's "negate" flag) comes from the sign of the dot product with the
  stored S_f.
- The map is verified: the recomputed geometry must reproduce the stored face within 1e-9 of the
  face size, otherwise the build throws (the mesh is not the geometry of its grid).

**`MeshGeometry::computeGeometry(map, vertices)` → the full geometry.**

- **2D** calls exactly the builder helpers `quadCellGeometry`, `verticalEdgeFace` and
  `horizontalEdgeFace` with the same arguments. For unchanged vertices the result is therefore bit
  for bit the geometry of `createStructuredQuad2D` / `createMultiBlock2D`, and round-off-equal to
  `createRectilinear2D`'s.
  - Invalid cells (non-convex, clockwise, zero area, degenerate edge) throw, naming the block, the
    cell (i, j) and the defect (`quadCellDefect`).
- **3D** treats each hexahedron as trilinear:
  - volume = ∫det J and centroid = ∫x det J / V, both by 2×2×2 Gauss, which is exact because the
    integrands are of degree ≤ 3 per variable;
  - face area vector S = ½ (p2 − p0) × (p3 − p1), the exact vector area of the bilinear face;
  - face centroid = the projected-area-weighted centroid of the 4-triangle fan about the vertex
    average, which is the exact area centroid for planar faces;
  - validity: all 8 corner Jacobians > 0, otherwise an error naming the cell (i, j, k) and the
    corner.
  - For unchanged Cartesian vertices it is round-off-equal to `createCartesian3D`.

### 3.4 Motion within a step, mesh velocity, swept volumes

- The prescribed motion gives vertex positions x(X, t). Within a step each vertex moves linearly in
  time from x^n = x(X, t^n) to x^{n+1} = x(X, t^{n+1}); this defines the discrete path.
- **Mesh velocity** of a vertex over the step is v = (x^{n+1} − x^n)/dt. This is the one place the
  difference is taken; no solver code computes it.
- **Swept volume** δV_f is the signed volume the face sweeps during the step, positive when the face
  moves along its stored S_f. It is exact for the discrete path:
  - 2D: δV_f = S_f(x^{n+½})·(Δp + Δq)/2, where x^{n+½} is the midpoint configuration and Δ is the
    vertex displacement. This is exact because the swept area is a bilinear integral.
  - 3D: δV_f = ∫₀¹∫∫ ẋ·(x_s × x_r) ds dr dt by 2×2×2 Gauss, exact because the integrand is of degree
    ≤ 2 per variable.
- **Volumetric mesh flux:** φ_m,f = δV_f/dt, equal to (u_mesh·S) averaged over the step.
- **Discrete GCL.** For every cell P: V_P^{n+1} − V_P^n = Σ_f s_Pf δV_f, where s_Pf = +1 if P owns
  f and −1 if P is its neighbour.
  - Both sides are exact integrals of the same polygon or trilinear geometry, so the identity holds
    to round-off (measured independently in logs/03).
  - The GCL is enforced by construction and verified every step: the per-cell and global residuals
    are recorded.
- **No-op steps.** If a step leaves every vertex bitwise where it was, no geometry is recomputed, the
  swept volumes and velocities are exactly 0, and `moved` = false. The static limit is therefore
  exact (bit for bit).

### 3.5 `PrescribedMotion` and `MeshMotion`

- `PrescribedMotion::position(X, t)` has these implementations:
  - `StationaryMotion`;
  - `AffineMotion(G, c, b)`: x = X + τ(G(X − c) + b) with τ = t − t₀. It covers translation,
    expansion or contraction, shear and axial (piston) compression, and vertex motion is linear in
    time;
  - `SinusoidalMotion(box, A, ω)`: x = X + A sin(ωτ) Π_d sin(π(X_d − lo_d)/L_d). It vanishes on the
    box boundary.
- Every motion has zero displacement at τ = 0. A 2D mesh refuses a motion with a z component.
- `MeshMotion(Mesh&, motion, t₀)` offers:
  - `advance(t)`: computes the new vertices and the kernel geometry, and only then commits (strong
    exception guarantee: on an invalid cell nothing changes). It returns a `MeshMotionStep` with:
    - V^n, δV_f, φ_m,f and the vertex velocities;
    - the per-cell and global GCL residuals;
    - min, max and total volume, max displacement, max speed, and `moved`.
  - `revert()`: restores the geometry, vertices and time of before the last `advance`. It is
    idempotent.
  - accessors for time, reference, current and previous vertices, and the last step.

### 3.6 ALE discretization (shared operators)

- `aleImplicitEulerTimeDerivative(mesh, φ^n, V^n, ρ, dt)` gives:
  - aP = ρV^{n+1}/dt, where V^{n+1} is the current mesh volume;
  - b = (ρV^n/dt)·φ^n.

  It uses the same operation order as `implicitEulerTimeDerivative`, so V^n == V gives the same
  bits.
- `relativeMassFlux(F, φ_m, ρ)` gives F_rel,f = F_f − ρ·φ_m,f, which is ρ(u − u_mesh)·S_f. With
  φ_m = 0 the result is F exactly.
- **Convection** is the existing shared `assembleConvectionContribution` with F_rel as the convecting
  flux. No new convection code is written.
- `assembleAleTransientMomentumComponent`: the same as `assembleTransientMomentumComponent`
  (effective-viscosity overload), but with the convecting flux F_rel and the ALE time term.
- **Continuity.** With the GCL satisfied, ρ(V^{n+1} − V^n)/dt + Σ(F − ρφ_m) = 0 reduces to ΣF = 0.
  The pressure correction therefore stays on the absolute flux F, unchanged. The GCL residual is
  exactly the difference, and G8 verifies it.
- **Uniform-flow argument.** For uniform u0, the momentum row residual is
  u0·[ρ(V^{n+1} − V^n)/dt + Σ F_rel] = u0·[ρ(GCL residual)/dt + Σ F^n]. That is, round-off plus the
  previous step's continuity residual.

### 3.7 `AlePISO` (2D)

`AlePISO final : TransientStepSolver` is constructed from `(MeshMotion&, fluid, velocity BCs,
pressure BCs, PISOSettings, referenceCell)` and is laminar only. `solveTimeStep(prev, dt)` does:

1. `requireTwoDimensional`; validate sizes and dt (InvalidConfiguration, mesh untouched).
2. `motion.advance(motion.time() + dt)`. An invalid cell throws `InvalidArgumentError`, and the mesh
   is unchanged.
3. Check boundary-motion consistency. If a face is inconsistent, revert and throw
   `InvalidArgumentError`, naming the patch and the mismatch. The rules:
   - A `Wall` face must have zero normal mesh velocity. A stationary wall may be slid along
     tangentially.
   - A `MovingWall(V_w)` face must have V_w·n̂ equal to its normal mesh velocity φ_m,f/|S_f|.
     Tangential difference is allowed (physical sliding).
   - A `Symmetry` face must have zero normal mesh velocity.
   - `Inlet`, `Outlet` and `FixedValue` faces are unconstrained.
   - Tolerance: 1e-8·(|V_w| + max vertex speed of the step).
   - The physical wall velocity always comes from the boundary condition. It is never derived from
     the mesh velocity.
4. The shared PISO step (`detail::solvePisoStep`), which contains the unmodified PISO algorithm, run
   with ALE terms:
   - V^n, and the convecting flux F_rel = F^n − ρφ_m for the momentum predictor;
   - CFL computed from F_rel on the new geometry.
5. A non-Converged step is reverted, then its status is returned. A Converged step leaves the mesh at
   t^{n+1}.

**Rejection by the time loop.** `TransientSolver` gains one hook, `onStepRejected()`, called
whenever it rejects a step. It defaults to a no-op, so static PISO is unchanged. `AlePISO` reverts
the mesh in it.

**Shared PISO step.** `PISO::solveTimeStep` becomes `detail::solvePisoStep(…, ale = nullptr)`: the
same calls in the same order. The static path is bit for bit unchanged, which G9.1 verifies.

**Diagnostics.**

- `evaluateAleConservation(mesh, step, F, ρ)` gives the per-cell ALE mass residual
  ρ(V^{n+1} − V^n)/dt + Σ s(F − ρφ_m), and its global sum.
- `checkBoundaryMotion(mesh, velocityBCs, step)` returns the consistency violations as messages.

### 3.8 Output

`VTKWriter` writes the vertex grids stored in the `Mesh`, which `setGeometry` updates. VTK output is
therefore always the current moved mesh. Nothing new is logged for static runs.

### 3.9 What is and is not claimed

**Verified:**

- geometry (analytically for affine motions; for non-affine motions against an independent Python
  implementation);
- mesh velocity (analytically) and the GCL (independent swept-volume formulas);
- the ALE flux sign and magnitude (hand-derived cases, Galilean invariance);
- exact-solution flows: uniform flow, the piston and translating-frame Couette;
- global and local mass conservation;
- the static limit (bit for bit).

**Not claimed:**

- AlePISO's accuracy on deforming meshes. It uses PISO's discretization (first-order implicit Euler,
  upwind convection, uncorrected diffusion), and no convergence-rate MMS on moving meshes is run;
- 3D ALE flow;
- time-dependent wall velocity (`MovingWall` is constant in time);
- restart of a moving-mesh run.
