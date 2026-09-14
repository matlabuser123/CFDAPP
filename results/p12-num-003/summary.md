# P12-NUM-003 — Non-orthogonal / Skewness Correction

**Status: COMPLETE — `[x]` in `TODO.md`** (2026-09-13). Every completion
gate in §21 passes with the measured evidence below. The remaining
limitations (§22) are disclosed. None of them is a failed gate.

The work ran in two stages. Both are recorded here; this is the only
evidence directory for the task.

- **Stage 1 (initial pass, previously recorded as PARTIAL).** It delivered
  mesh-quality metrics, one over-relaxed face-area decomposition, the
  corrected diffusion operator, the corrected momentum viscous term, the
  `non_orthogonal_corrections` config with its momentum pass loop, and a
  skew-corrected interpolation primitive. It stopped at a blocker: the
  pressure-correction equation was axis-aligned-only.
- **Stage 2 (continuation, this update).**
  - **A.** A geometric pressure-correction equation (one implementation,
    shared by incompressible and compressible SIMPLE).
  - **B.** Distorted-mesh SIMPLE, end to end.
  - **C.** The N-pass pressure-correction loop.
  - **D.** CompressibleSIMPLE on distorted meshes.
  - **E.** Skewness correction in the production Green-Gauss gradient.
  - **F–H.** Thermal, species and turbulence diffusion through the shared
    corrected face-flux helper.
  - **I.** Gradient-scheme selection for the correction.
  - **J.** Diagnosis of the boundary ring's first-order error.

Environment: WSL2 Ubuntu, GCC, `build/debug` (Debug), 2026-09-13. Every
number below was printed by a committed test (reproduce with the test named
next to it) or by the Cartesian-equivalence dump program described in §5.
Nothing is estimated.

---

## 1. Baseline (before this task)

- Full regression at the end of P12-NUM-002: **1417/1417**. At the end of
  stage 1: **1478/1478** (13 disabled).
- `Diffusion.cpp` internal faces: plain `Gamma*|Sf|*(phiN-phiP)/|d|`.
  Boundary faces: a one-sided chain fit normal to the boundary (P0 design).
- Pressure correction (`PressureCorrectionEquation.cpp`,
  `CompressiblePressureCorrection.cpp`): `D_f = rho*|Sf|*d_comp/|d|` with
  `d_comp = d_u` on x-normal faces and `d_v` on y-normal faces. It **threw**
  on any face that was not axis-aligned.
- Thermal, species and turbulence each assembled their own inline two-point
  diffusion coefficient.
- The Green-Gauss gradient used plain distance-weighted face values (not
  skew-corrected).

### Audit of diffusion and pressure consumers (by `grep`), with final status

| Consumer | Implementation after P12-NUM-003 | Status |
|---|---|---|
| `cfd::discretization::diffusion` / `laplacian` | `Diffusion.cpp` → shared `internal/boundaryFaceDiffusionTerms` | **Corrected** (flag, default off) |
| Momentum viscous term (`RelaxedMomentum` → SIMPLE; `CompressibleRelaxedMomentum` → CompressibleSIMPLE) | `assembleDiffusionContribution` → shared helper | **Corrected, wired to config** |
| Pressure correction (SIMPLE, CompressibleSIMPLE, PISO) | `assembleGeometricPressureCorrection` (one implementation) | **Geometric**; over-relaxed + N passes in SIMPLE/CompressibleSIMPLE |
| Thermal (`EnergyEquation`, `ThermalSolver`) | shared helper | **Corrected, wired via `ProjectRunner`** |
| Species (`SpeciesEquation`, `SpeciesSolver`) | shared helper | **Corrected, wired via `ProjectRunner`** |
| Turbulence k/ε/ω (`KEpsilonEquation`; k-ε, k-ω and SST models) | shared helper | **Corrected, wired via `CaseBuilder`** |
| `TransientMomentum` / PISO momentum | `assembleDiffusionContribution` (defaults) | not corrected (§22) |
| Conjugate `ThermalInterface` | own inline coefficient | not corrected (§22) |

---

## 2. Geometric decomposition (one authoritative formula)

`MeshGeometry::decomposeAreaVector(d, S)` is used by `decomposeFaceArea`
(internal face, `d = x_N − x_P`), by `decomposeBoundaryFaceArea` (boundary
face, `d = x_f − x_P`), and by the pressure-correction coupling (applied to
the response vector `S_D`, §6). It is the over-relaxed approach (Jasak 1996;
Moukalled, Mangani & Darwish, *The Finite Volume Method in CFD*, §8.6):

```
S_orth    = (S·S / d·S) d          (parallel to d)
S_nonorth = S − S_orth
```

- Why over-relaxed: `|S_orth| = |S|/cos θ ≥ |S|`, so the implicit
  coefficient is never weaker than the uncorrected one.
- **Exact on orthogonal faces.** When `d × S` is exactly zero, the function
  returns `{S, 0}` directly. This makes every corrected operator
  bit-identical on Cartesian meshes.
- **Well-posedness guard.** It returns `valid = false` (vectors `{0,0}`,
  never NaN/Inf) when `|d| = 0`, when `|S| = 0`, or when
  `d·S ≤ 10⁻⁶ |d||S|`.
- Verified by `MeshGeometryNonOrthogonal.*`:
  - a hand-derived 30° face;
  - reconstruction `S_orth + S_nonorth = S` to 1e-14;
  - `S_orth ∥ d`;
  - `|S_orth| ≥ |S|` on every distorted face.

---

## 3. Mesh-quality metrics

These are per internal face (`MeshQualityReport` max/mean).

- **Non-orthogonality angle:** `θ = acos(d·Sf / (|d||Sf|))`.
- **Skewness:** `|x_f − x_f'| / |d|`, where `x_f'` is where the
  owner–neighbor line crosses the face line.

The two are distinct effects: `OrthogonalButSkewedFace` has θ = 0 and
skewness 0.3; `NonOrthogonalButUnskewedFace` has θ = 30° and skewness 0.

`MeshQuality.MetricsDiscriminateDistortionLevels`, 16×16, amplitude as a
fraction of h:

| level | amp/h | max θ (°) | mean θ (°) | max skew | mean skew | min cell area |
|---|---|---|---|---|---|---|
| Cartesian | 0.00 | 0 | 0 | 0 | 0 | 0.00390625 |
| mild | 0.10 | 2.45144 | 1.05684 | 0.00384443 | 0.00106428 | 0.0038123 |
| moderate | 0.25 | 6.119 | 2.64151 | 0.00981648 | 0.00266405 | 0.00367275 |
| strong | 0.45 | 10.9764 | 4.75183 | 0.0181881 | 0.00480607 | 0.00348924 |

The meshes come from the existing P12-NUM-002 generator
`tests/unit/discretization/DistortedMesh.hpp` (`createDistortedQuad2D`),
reused for every distorted test in this task; no second generator exists.
Its perturbation vanishes on the domain boundary.

---

## 4. Shared corrected diffusion face flux (one implementation)

`include/cfd/discretization/NonOrthogonalDiffusion.hpp` is the single
implementation that every corrected diffusion term calls:
`internalFaceDiffusionTerms`, `boundaryFaceDiffusionTerms`,
`prescribesBoundaryValue` and `NonOrthogonalCorrectionOptions {enabled, gradientScheme}`.

- **Internal face:** coefficient `Γ_f |S_orth| / |d|` (implicit) and
  explicit flux `Γ_f S_nonorth · (∇φ)_f`. Here `(∇φ)_f` is the
  distance-weighted face interpolation of P12-NUM-002's cell gradient, with
  the scheme taken from the options.
- **Value-prescribing boundary face** (`prescribesBoundaryValue`:
  FixedValue, FixedTemperature, WallOmega, Wall, MovingWall, Inlet):
  - the pre-existing boundary flux is scaled by `|S_orth,b|/|Sf|`;
  - `Γ S_nonorth,b · (∇φ)_P` is added;
  - the split is taken against `d = x_f − x_P`.
- **Flux-prescribing boundary face** (FixedGradient, HeatFlux, Adiabatic,
  Outlet, Symmetry): **never corrected.** The complete classification is
  pinned by `DiffusionTest.BoundaryCorrectionClassificationCoversEveryConditionType`.
- **Degenerate face** (`valid = false`): the uncorrected formula is used for
  that face.
- **Face-once conservation:** each face flux is computed once and added to
  the owner / subtracted from the neighbor. On a Cartesian mesh every term
  is exactly the uncorrected one.

**Callers:**
- `Diffusion.cpp`;
- `MomentumEquation.cpp` (both `assembleDiffusionContribution` overloads);
- `EnergyEquation.cpp`;
- `SpeciesEquation.cpp`;
- `KEpsilonEquation.cpp` (the k, ε and ω transport of all three models).

No module keeps a private corrected-flux formula.

**Why the boundary-face correction is required** (measured in stage 1).
With the correction on internal faces only, the boundary ring had an O(1)
error that did not converge (global L∞ 0.818 → 0.751 on 8 → 64). A boundary
cell had its internal face corrected but not the opposite boundary face, so
the O(θ/h) correction contributions no longer cancelled. With the boundary
correction the ring converges (§16).

---

## 5. Orthogonal-path compatibility (critical gate)

**Library-level equivalence, all production paths, bit for bit.** A dump
program (`baseline_dump.cpp`, a scratch evidence helper built against the
library) writes 2439 lines of 17-significant-digit results on Cartesian
meshes:

| Path | Iterations |
|---|---|
| SIMPLE lid-driven cavity, 12×10, ρ ≠ 1 (exercises operand order), GreenGauss and LeastSquares | 60 each, full u/v/p/flux and every residual-history entry |
| SIMPLE open channel (Inlet → FixedValue-pressure outlet) | 80 |
| PISO, three time steps | 3 steps |
| CompressibleSIMPLE air cavity | 50 |
| CompressibleSIMPLE open channel | 60 |
| Thermal solve | 186 |
| Species solve | 118 |
| Turbulence relaxed scalar transport | — |

The output from the library **before** the continuation and from the
**final** library are byte-identical (`cmp`: identical).

The Cartesian short-circuits that make this exact:
- `decomposeAreaVector` returns `{S, 0}` exactly.
- On an axis-aligned face whose `d` is exactly parallel to Sf, the pressure
  coupling is evaluated as `rho*|Sf|*d_comp/|d|`, the legacy expression in
  its original operand order.
- The skew-corrected Green-Gauss never touches a face whose skew vector is
  exactly zero.
- `ownerNeighborCrossing` returns the centroid exactly when it lies on the
  owner–neighbor line.

**Test-level checks:**

| Check | Result |
|---|---|
| `PressureCorrectionNonOrthogonalTest.CartesianCompatibility`: every face coefficient vs an independent hand-coded legacy formula (x/y faces, Dirichlet boundary faces), two-point and over-relaxed options; matrix and RHS | **EXPECT_EQ** on every value; explicit pass adds exactly 0 |
| `SIMPLENonOrthogonalTest.NonOrthogonalCorrectionsZeroPreservesBaseline`: default vs explicit `N=0`; `N=0` vs `N=1` for GreenGauss and LeastSquares (4×4 cavity) | bit-identical (every history entry, u, v, p, flux) |
| `SIMPLENonOrthogonalTest.CartesianExtraPassesOnlyDifferByLinearSolverTolerance`: `N=3` vs `N=0` | same 390 iterations, max\|Δu\| 5.72e-17 |
| `CompressibleSIMPLENonOrthogonalTest.CartesianCompatibility`: full solve `N=1` vs `N=0`; compressible p′ assembly, two-point vs over-relaxed | bit-identical (u, v, p, ρ, flux; matrix values) |
| `ThermalNonOrthogonalTest.CartesianSolveIsBitIdentical`, `SpeciesNonOrthogonalTest.CartesianSolveIsBitIdentical` (both gradient schemes; FixedTemperature + HeatFlux + Adiabatic, FixedValue + FixedGradient) | bit-identical |
| `SkewnessTest.ProductionInterpolationUsesCorrection` (Cartesian half): production gradient vs plain 0-sweep Green-Gauss | bit-identical |
| `GridRefinementTest.LaplacianOfSmoothFieldConvergesAtSecondOrder` | unchanged: L2 0.0938975 / 0.0277444 / 0.00744658 / 0.00192272 (p 1.759 / 1.898 / 1.953) |
| Full regression | every pre-existing test passes (§20) |

---

## 6. Pressure-correction redesign (Phase A)

`include/cfd/pressure_velocity/PressureCorrectionEquation.hpp` has **one**
face-coupling formula, `pressureCorrectionFaceCoupling`, and **one**
assembly, `assembleGeometricPressureCorrection`. The callers:
- incompressible `assemblePressureCorrection`: uniform density, no extra
  diagonal;
- compressible `assembleCompressiblePressureCorrection`: per-face density
  plus its `V/Δt·∂ρ/∂p` diagonal;
- SIMPLE, CompressibleSIMPLE and PISO, all through those two.

**Derivation.** SIMPLE's velocity correction is `u′ = −D ∇p′`, with the
anisotropic response `D = diag(d_u, d_v)`, `d = V/a_P`. The face mass-flux
correction is therefore

```
F′_f = −ρ_f (D_f ∇p′_f)·Sf = −ρ_f ∇p′_f · S_D,     S_D = (d_u,f Sf.x, d_v,f Sf.y)
S_D  = E + T,  E = (S_D·S_D / d·S_D) d             (§2's decomposition applied to S_D)
F′_f = [ρ_f |E|/|d|] (p′_P − p′_N)  −  ρ_f T·∇p′_f
```

(Moukalled et al., ch. 15.) The response vector `S_D`, not `Sf`, is split.
That is what keeps the formulation consistent with the anisotropic `D`
already used by `correctVelocity`. On an axis-aligned face `S_D = d_comp Sf`
exactly, so the two formulations coincide.

**Options** (`PressureCorrectionOptions`):
- `nonOrthogonal = false`: two-point coupling `ρ_f|S_D|/|d|`, no explicit
  term.
- `nonOrthogonal = true`: over-relaxed coefficient `ρ_f|E|/|d|`. With
  `previousPressureCorrection` supplied, the explicit term
  `−ρ_f T·∇p′_prev` goes to the RHS. `∇p′` uses the same p′ boundary
  conditions as `correctVelocity`: 0 at FixedValue pressure patches,
  zero-gradient elsewhere.

**Boundaries.**
- A Dirichlet (FixedValue) pressure face is coupled with the owner's
  `d_u, d_v` and `d = x_f − x_P`.
- Every other boundary face has exactly zero coupling and zero explicit
  flux (walls and inlets keep their prescribed flux).
- Reference-cell pinning is unchanged.

**Degenerate geometry.**
- A split with `d·S_D` not safely positive falls back to the finite
  two-point coupling for that face.
- A non-finite coupling (e.g. `|d| = 0`) throws `NumericalError`, which
  SIMPLE and CompressibleSIMPLE report as `NonFiniteState`. Before this
  task it was an unrelated `SparseMatrix` InvalidArgumentError.

**Hand-derived verification**
(`PressureCorrectionNonOrthogonalTest.NonOrthogonalInternalFace`):
- Setup: d = (1, 0), Sf at 30°, d_u = 2, d_v = 1, ρ = 1.5.
- The over-relaxed coefficient ρ|E| and the explicit vector ρT match the
  closed form to 1e-14.
- The two-point coefficient ρ|S_D| matches.
- Over-relaxed ≥ two-point.

`NonOrthogonalBoundary` checks the Dirichlet-face coefficient against the
shared formula with owner data and d = x_f − x_P (1e-13 relative), and
checks exact zeros on Neumann faces. `DegenerateGeometryRejected` checks
both degenerate cases.

---

## 7. Face-flux consistency (Rhie-Chow status)

**What exists.** This codebase has **no Rhie-Chow interpolation**, before or
after this task. The predictor face flux is
`ρ (interp(u*)·Sf)` (`MassFlux.hpp`), and pressure-velocity coupling relies
on the SIMPLE correction step alone. That flux is already geometry-generic
(it only uses `Sf`), and it is unchanged. Adding Rhie-Chow would change
every existing result, so it is outside this task.

**What is kept consistent.** In the collocated coupling, the matrix, the
flux update and continuity must use one face-flux representation:
- `PressureCorrectionAssembly::faceCoefficient` is reused by
  `correctFaceMassFlux`, as before.
- The new `PressureCorrectionAssembly::explicitFaceFlux` (the
  `−ρ_f T·∇p′` terms, owner-oriented) is added face by face by
  `correctFaceMassFlux(..., &explicitFaceFlux)` whenever the final pass had
  an explicit term.
- The corrected flux therefore satisfies exactly the discrete continuity
  equation that the final assembly encodes.

Measured (`PressureCorrectionNonOrthogonalTest.NonOrthogonalConservation`,
distorted 9×9 0.45h, pass 2):

| | max cell continuity after the flux update |
|---|---|
| explicit face terms included (production) | **5.24e-13** |
| explicit face terms omitted (inconsistent) | 0.0157 |

The maximum |explicit face flux| was 0.03, so the term is active. The
global net flux of the corrected closed-domain flux is 0 (to 1e-14).

---

## 8. N-pass pressure-correction loop (Phase C)

`solver.json` `non_orthogonal_corrections = N` (SIMPLE and CompressibleSIMPLE):

- **N = 0:** two-point coupling, one pressure solve, exactly the
  pre-P12-NUM-003 equation on a Cartesian mesh. On a distorted mesh it is
  the uncorrected geometric equation (before this task: a throw).
- **N ≥ 1:** over-relaxed coefficient. Pass 1 has no explicit term: in this
  incremental formulation p′ starts every outer iteration at 0. Passes 2..N
  re-assemble with `−ρT·∇p′_(k−1)` and re-solve. p′ is applied **once**,
  after the last pass, to the pressure, the velocity and the face flux (§7).
  OpenFOAM's `nNonOrthogonalCorrectors` carries the previous *pressure*, so
  its count is offset by one.
- Each pass solve starts from a **zero initial guess**. Warm-starting from
  `p′_(k−1)` begins at a residual only as large as the explicit term's
  change. The unpreconditioned BiCGSTAB's absolute breakdown thresholds
  were measured to trip at that scale (Breakdown at ~2e-10 from a 5e-8
  start). The exact solution is the same either way.
- The momentum predictor keeps stage 1's N-pass loop
  (`runNonOrthogonalCorrectionPasses`). `SIMPLEResult` (and
  `CompressibleSIMPLEResult`) gained `momentumPredictorPasses` and
  `pressureCorrectionPasses`.

| Test | Result |
|---|---|
| `SIMPLENonOrthogonalTest.NonOrthogonalCorrectionPassCount` (distorted 6×6, fixed 7 iterations, N = 0/1/2/4) | pressure and momentum passes = 7 × max(1, N), exactly |
| distorted-cavity runs (§9), every N | passes p/u = iterations × max(1, N) (e.g. 1013 iterations, N = 2 → 2026/2026) |
| `SIMPLENonOrthogonalTest.NonOrthogonalCorrectionsZeroPreservesBaseline` (distorted 8×8 0.45h) | N = 0 runs one pass of each per iteration; \|u(N=0) − u(N=1)\| = **0.012**, so 0 really means off |
| momentum corrector iteration, distorted 12×12 (0.4h), 8 passes | increments 1.11e-3, 3.62e-5, 1.15e-6, 5.2e-8, 2.53e-9, 1.01e-10, 6.32e-12; fully-corrected residual 3.22e-4 → 1.66e-13 |

---

## 9. Distorted-mesh SIMPLE (Phase B)

Lid-driven cavity, Re = 100 (ρ = 1, μ = 0.01, lid u = 1), 10×10
`createDistortedQuad2D`, all-zero-gradient pressure with reference-cell
pinning, cavity tolerances 1e-6, linear solves to 1e-10 absolute
(`SIMPLENonOrthogonalTest.DistortedMesh{Mild,Moderate,Strong}`). Every run
converged with finite values. "Continuity" is SIMPLE's per-iteration RMS
cell imbalance of the corrected flux. `||b_p||` is the predictor mass
imbalance that each pressure correction removes; it is the quantity that
genuinely decreases, since the corrected flux satisfies continuity every
iteration.

| mesh | scheme | N | iterations | final continuity | max continuity (history) | global imbalance | ‖b_p‖ first → last | passes p/u |
|---|---|---|---|---|---|---|---|---|
| mild 0.10h | LS | 0 | 1001 | 7.99e-11 | 3.64e-10 | 0 | 0.0503 → 9.98e-7 | 1001/1001 |
| mild 0.10h | LS | 1 | 1002 | 9.63e-11 | 2.07e-10 | 0 | 0.0503 → 9.99e-7 | 1002/1002 |
| mild 0.10h | LS | 2 | 1002 | 7.71e-11 | 3.20e-10 | 0 | 0.0503 → 9.98e-7 | 2004/2004 |
| moderate 0.25h | LS | 0 | 1006 | 7.96e-11 | 2.24e-10 | 0 | 0.0503 → 9.97e-7 | 1006/1006 |
| moderate 0.25h | LS | 1 | 1010 | 9.64e-11 | 2.43e-10 | 0 | 0.0502 → 9.99e-7 | 1010/1010 |
| moderate 0.25h | LS | 2 | 1010 | 7.22e-11 | 1.84e-10 | 0 | 0.0502 → 9.97e-7 | 2020/2020 |
| strong 0.45h | GG | 0 | 996 | 9.36e-11 | 4.07e-10 | 0 | 0.0502 → 9.97e-7 | 996/996 |
| strong 0.45h | GG | 1 | 1004 | 7.13e-11 | 2.65e-10 | 0 | 0.0501 → 9.97e-7 | 1004/1004 |
| strong 0.45h | GG | 2 | 1004 | 7.06e-11 | 2.52e-10 | 0 | 0.0501 → 9.95e-7 | 2008/2008 |
| strong 0.45h | LS | 0 | 1007 | 9.25e-11 | 4.07e-10 | 0 | 0.0502 → 9.99e-7 | 1007/1007 |
| strong 0.45h | LS | 1 | 1015 | 9.84e-11 | 2.01e-10 | 0 | 0.0501 → 9.96e-7 | 1015/1015 |
| strong 0.45h | LS | 2 | 1013 | 7.93e-11 | 2.76e-10 | 0 | 0.0501 → 9.99e-7 | 2026/2026 |

- **Mass conservation of the converged flux field**
  (`NonOrthogonalMassConservation`, 0.45h, N = 2, LS): max cell imbalance
  **7.87e-10**, global net flux **0**, max |wall face flux| **0**. The
  walls, lid included, are exactly impermeable.
- **Determinism** (`NonOrthogonalDeterministic`, 8×8 0.45h, N = 3, GG): two
  runs are bit-identical (every history entry, u, v, p, flux, pass count).
- **Analytical flow — plane Couette**
  (`CorrectedSolveIsCloserToExactCouetteFlowOnDistortedMesh`): u = (y, 0)
  with constant p is an exact steady Navier–Stokes solution. It was imposed
  through exact per-face MovingWall values on distorted 0.25h meshes (LS,
  Jacobi-preconditioned linear solves; §22 explains why).

| | L2 velocity error n = 8 | n = 16 | observed order |
|---|---|---|---|
| uncorrected (N = 0) | 2.670e-3 | 6.91e-4 | 1.950 |
| **corrected (N = 1)** | 2.353e-3 | **2.949e-4** | **2.996** |

The corrected solve is 2.3× more accurate at n = 16 and converges faster.
The gates are corrected < 0.6 × uncorrected at n = 16, and corrected order
> uncorrected order + 0.5.

---

## 10. CompressibleSIMPLE (Phase D)

These tests use the same principles and the same shared assembly (§6).
`CompressibleSIMPLESettings` gained `nonOrthogonalCorrections` and
`gradientScheme`, with the same pass loop and zero-guess pass solves.
`CompressibleRelaxedMomentum` passes the viscous correction options and
takes its pressure-gradient scheme from the same `gradientScheme`. Before
this task that gradient was hard-wired to GreenGauss. That mismatch was
found as a 6e-3 low-Mach discrepancy on distorted meshes and fixed.

| Test | Result |
|---|---|
| `CartesianCompatibility` | bit-identical full solve (N = 1 vs 0) and assembly (§5) |
| `NonOrthogonalPressureCorrection`: air (R = 287.05, cp = 1005, p_ref = 101325, T = 300), μ = 1.8e-5, cavity, distorted 8×8 0.45h, N = 0 | 394 iterations; final continuity 2.22e-11; max cell imbalance 1.54e-10; global imbalance 0; max \|wall flux\| 0; ρ ∈ [1.17662427, 1.17662429]; 394 pressure passes |
| same, N = 2 | 394 iterations; final continuity 1.31e-11; max cell imbalance 6.78e-11; global 0; wall flux 0; ρ ∈ [1.17662427, 1.17662429]; 788 pressure passes |
| `NonOrthogonalOpenChannel`: Inlet (0.5, 0) → FixedValue-pressure outlet, distorted 12×6 0.3h, N = 2 | 2214 iterations; inlet flux −0.294156073, outlet +0.294156074; global net 7.37e-10; max cell imbalance 1.79e-11 |
| `LowMachRegression` (**low-Mach limit on a distorted mesh**, 6×6 0.3h, N = 2, LS): gas with R = 1e10 vs incompressible SIMPLE, same mesh and settings | max \|u_compressible − u_SIMPLE\| **7.75e-10**; max \|ρ − 1\| 6.73e-14 (SIMPLE 816 iterations, compressible 584) |
| EOS behavior | density stays finite and positive in every distorted run; the EOS path is unchanged (bit-identical on Cartesian, §5) |
| Arkilic et al. (1997) production case (`CompressibleCoupledProductionCaseTest.MatchesArkilicIsothermalLubricationPressureProfile`, Cartesian) | passes in the full regression (L2 and L∞ < 5% gate). Its Cartesian production path is covered by the §5 bit-identity result. The error values themselves were not re-printed in this run. |

All 23 `CFDCompressibleSimpleTests` pass (`focused_tests.log`).

---

## 11. Skewness correction in production (Phase E)

**What is wired.** `cfd::discretization::gradient(..., GreenGauss)` is now
the skewness-corrected Green-Gauss gradient, `greenGaussGradient(mesh, φ,
bcs, kGreenGaussSkewCorrectionSweeps)`:
1. compute the plain Green-Gauss sum;
2. for `kGreenGaussSkewCorrectionSweeps` sweeps, re-evaluate every skewed
   internal face with `interpolateInternalFaceSkewCorrected`,
   `φ_f = φ_f′ + ∇φ_f′·(x_f − x_f′)`, using the previous sweep's gradient,
   and re-sum.

`computeVelocityGradient` (GreenGauss) applies the same sweeps. There is
one skew-corrected face-value primitive (`Interpolation.hpp`, scalar and
`Vector2` overloads). A face whose skew vector is exactly zero is never
touched (Cartesian bit-identity, §5).

**What consumes it.** Every production consumer of the Green-Gauss
gradient:
- the momentum pressure-gradient source;
- `correctVelocity`;
- LinearUpwind convection (the only convection scheme that uses a gradient);
- the non-orthogonal correction's `(∇φ)_f` in every diffusion term;
- SST cross-diffusion;
- the thermal and species gradients.

`kGreenGaussSkewCorrectionSweeps = 4` is **empirical**. It is chosen from
the sweep study below: each sweep contracts the linear-field error about
100–1000×, while the smooth-field error stops improving after 1–2 sweeps.
With 4 sweeps the worst measured linear-field error is ≤ 1.1e-9.

**Quantitative improvement in production operators:**

| Measurement | plain (0 sweeps) | production (skew-corrected) |
|---|---|---|
| GG gradient of φ = 2x + 3y + 5, distorted 10×10 0.10h, max error (`SkewnessTest.CorrectedProductionOperatorImprovesError`) | 0.0173 | **4.68e-13** |
| same, 0.25h | 0.0459 | **5.17e-11** |
| same, 0.45h | 0.0876 | **1.13e-9** |
| velocity gradient (`computeVelocityGradient`), linear field, distorted 9×9 0.4h | 0.1043 (stage 1) | **1.517e-9** (LS: 7.77e-15) |
| corrected diffusion, max interior \|∇²φ_h\| for linear φ, distorted 12×12 0.3h, GG-fed correction | 0.018 (stage 1) | **9.25e-12** (LS: 7.04e-13; uncorrected operator 2.917) |
| corrected momentum diffusion, linear velocity, distorted 10×10 0.3h, max\|Au − b\| U / V, GG-fed | 1.32e-4 / 1.54e-4 (stage 1) | **3.10e-13 / 3.91e-13** (LS 5.3e-15 / 1.3e-15; uncorrected 0.01294 / 0.01813) |
| k/ε/ω diffusion operator, linear scalar, distorted 10×10 0.4h, max\|Aφ − b\|, GG-fed | — | **2.11e-14** (LS 4.16e-17; uncorrected 2.399e-4) |

**Sweep study** (max error of the GG gradient of the linear field,
distorted 10×10):

| amp | 0 | 1 | 2 | 3 | **4** | 5 |
|---|---|---|---|---|---|---|
| 0.10h | 0.0173 | 1.67e-5 | 5.71e-8 | 9.08e-11 | **4.68e-13** | 3.54e-14 |
| 0.25h | 0.0459 | 1.14e-4 | 9.68e-7 | 3.97e-9 | **5.17e-11** | 1.96e-13 |
| 0.45h | 0.0876 | 4.08e-4 | 6.27e-6 | 4.82e-8 | **1.13e-9** | 8.26e-12 |

`SkewnessTest.ProductionInterpolationUsesCorrection` pins the wiring:
- on distorted 9×9 (0.4h), production `gradient()` equals
  `greenGaussGradient(kSweeps)` bit-for-bit and differs from the 0-sweep
  formula by more than 1e-4;
- on Cartesian it equals the plain formula bit-for-bit.

Primitive check (stage 1): the skew-corrected interpolation of a linear
field on distorted 10×10 (0.3h, max skew 0.0292) has max face error
1.78e-15, vs 0.00838 for plain interpolation.

**Not skew-corrected (disclosed):**
- the predictor mass flux `ρ(interp(u*)·Sf)`;
- Central/QUICK convection face values;
- the diffusion face gradient, whose skew correction would need a Hessian.

The gradient is the production operator through which skewness enters
every corrected term.

---

## 12. Thermal / species / turbulence integration (Phases F, G, H)

All three call the shared `NonOrthogonalDiffusion` helper (§4). There is no
copy of the formula in any of them.

- **Thermal:** `EnergyEquation` (constant- and variable-property
  assemblers) and `ThermalSolver` (`settings.nonOrthogonal`). HeatFlux and
  Adiabatic walls are never corrected; FixedTemperature walls are.
- **Species:** `SpeciesEquation` and `SpeciesSolver`
  (`settings.nonOrthogonal`).
- **Turbulence:** `KEpsilonEquation` (k, ε and ω transport) and the k-ε,
  k-ω and SST models (`nonOrthogonal` member). WallOmega is value-
  prescribing.

The config wiring is one function, `pressure_velocity::nonOrthogonalOptions(SIMPLESettings)`,
giving `{N > 0, gradientScheme}`. `ProjectRunner` uses it for thermal and
species, and `CaseBuilder` uses it for the turbulence model.
`TurbulenceCaseTest.NonOrthogonalCorrectionReachesEveryTurbulenceModel`
and `SIMPLESettingsTest.NonOrthogonalOptionsFollowTheCorrectionCountAndGradientScheme`
pin that wiring.

### Thermal — analytical conduction (`ThermalNonOrthogonalTest.NonOrthogonalDiffusion`)

The case is steady conduction with a source on distorted 0.45h meshes,
with exact per-face FixedTemperature values from a manufactured analytical
field. Grids are 8/16/32; the order p is taken between successive grids.

| variant | L1 (8, 16, 32) | p | L2 (8, 16, 32) | p | L∞ (8, 16, 32) | p | energy balance Σ(AT − b) |
|---|---|---|---|---|---|---|---|
| uncorrected | 0.02703, 0.01200, 0.005651 | 1.172, 1.086 | 0.03536, 0.01619, 0.007677 | 1.127, 1.077 | 0.07606, 0.03641, 0.01761 | 1.063, 1.048 | 1.31e-11, −3.4e-12, 7.08e-11 |
| **corrected (GG)** | 0.004982, 0.001266, 0.0003173 | **1.976, 1.996** | 0.005428, 0.001353, 0.0003374 | **2.004, 2.004** | 0.008263, 0.002674, 0.0007485 | 1.628, 1.837 | 1.41e-11, 3.13e-12, −2.29e-12 |
| **corrected (LS)** | 0.005172, 0.001278, 0.0003181 | **2.017, 2.006** | 0.005619, 0.001364, 0.0003381 | **2.042, 2.013** | 0.008712, 0.002681, 0.0007487 | 1.700, 1.840 | 1.4e-11, 1.18e-11, 1.36e-11 |

- At 32×32 the corrected L2 is 22.7× smaller than uncorrected.
- The gate checks that it is within 1.1× of the **Cartesian** solve's L2
  at the same resolution.
- **Prescribed-flux walls** (`PrescribedFluxWallsKeepEnergyBalance`,
  distorted 10×10 0.4h, FixedTemperature + HeatFlux(−150) + Adiabatic,
  corrected LS): converged in 533 iterations; global energy balance
  Σ(AT − b) = **−1.39e-9** (gate 1e-8).
- This test's outer-iteration cap is 3000 (the `solverSettings()` default
  is 500). The pre-existing HeatFlux Picard lag contracts only ~5% per
  outer iteration on this case, measured the same **with or without** the
  correction. The cap is not a tolerance, and the converged answer meets
  the unchanged 1e-8 balance gate.

### Species (`SpeciesNonOrthogonalTest.NonOrthogonalDiffusion`)

Diffusion with a source on distorted 0.45h meshes, with exact per-face
FixedValue concentrations.

| | L2 n = 8 | n = 16 | n = 32 | p (8→16, 16→32) |
|---|---|---|---|---|
| uncorrected | 0.1086 | 0.05219 | 0.02532 | 1.057, 1.043 |
| **corrected (LS)** | 0.01504 | 0.003547 | **0.000864** | **2.084, 2.037** |

- Corrected is 29× smaller at n = 32.
- Global species balance Σ(Ac − b), uncorrected / corrected:
  - n = 8: 1.4e-11 / 2.04e-10;
  - n = 16: 5.84e-11 / 4.74e-10;
  - n = 32: 1.8e-10 / 1.18e-9.
- The gate is 1e-8. That bound is set by the linear solve: a sum of N row
  residuals of a system solved to ‖r‖₂ ≤ 1e-10 can reach √N·1e-10 ≈ 3e-9 at
  32×32. An artificial source would appear at the explicit-flux scale
  (~1e-2).
- The test's linear-solver absolute and relative tolerance is 1e-10, the
  repository's usual value. The Picard loop's late warm-started solves start
  near 1e-8, and BiCGSTAB was measured to stall just above 1e-11 (a
  pre-existing solver behavior; an earlier 1e-11 setting gave
  `LinearSolveFailure`). The balance gate is derived from that tolerance as
  described above.

### Turbulence (`TurbulenceNonOrthogonalTest`)

- **`NonOrthogonalDiffusion`**, operator level: the diffusion part of the
  k/ε/ω transport equation (`solveRelaxedScalarTransport`'s assembler),
  linear scalar, distorted 10×10 0.4h, constant Γ = 0.02, exact Dirichlet
  values. max|Aφ − b| is 2.399e-4 uncorrected, **2.11e-14** corrected (GG)
  and **4.16e-17** corrected (LS). Its second half checks that on a
  Cartesian mesh with variable Γ and FixedValue + FixedGradient boundaries,
  the corrected matrix and RHS are bit-identical to the uncorrected ones.
- **`ModelsRouteTheCorrectionThroughTheSharedOperator`**: `KEpsilonModel`
  and `KOmegaModel` with the correction enabled differ from uncorrected on
  a distorted mesh after `correct()`, stay finite and positive, and are
  bit-identical on Cartesian. SST uses the same shared k/ω transport
  assembler. Its config wiring is pinned by
  `TurbulenceCaseTest.NonOrthogonalCorrectionReachesEveryTurbulenceModel`
  (k-ε, k-ω and SST case files).
- **Not in scope:** a turbulent-flow solution-level study (e.g. channel
  flow) on a distorted mesh would be P12-TURB.

---

## 13. Gradient choice for the correction (Phase I)

The correction's `(∇φ)_f` is **configured, not automatic**. It uses the
case's `gradient_scheme` (P12-NUM-002 key; `green_gauss` default,
`least_squares` optional) through `NonOrthogonalCorrectionOptions::gradientScheme`
in every consumer. It also sets the gradient scheme of the pressure
explicit term and of the compressible pressure source.

Honest note on Green-Gauss. Before stage 2's skew correction, a GG-fed
correction was not linear-exact on skewed meshes (0.018 residual, §11).
With the skew-corrected production GG it is exact to ~1e-9–1e-12.
LeastSquares is exact to round-off. Both schemes are exercised in:
- `SIMPLENonOrthogonalTest.DistortedMeshStrong` (GG and LS);
- the thermal three-way table (§12);
- the momentum, turbulence, velocity-gradient and diffusion linear-field
  tests (§11).

Smooth-field accuracy is essentially identical for the two (thermal L2
0.0003374 GG vs 0.0003381 LS at 32×32).

`docs/user_guide/case_format.md` documents both.

---

## 14. Manufactured diffusion convergence (operator level, stage 1)

Field `φ = sin(πx) cos(πy)`, exact `∇²φ = −2π²φ`, Γ = 1, exact per-face
Dirichlet values, grids 8/16/32/64. **Global** means all cells; **Interior**
means cells with no boundary face.

### Orthogonal (Cartesian), uncorrected — and correction on (identical)

| Grid | L1 | p | L2 | p | L∞ | p |
|---|---|---|---|---|---|---|
| 8 | 0.072895 | – | 0.093898 | – | 0.20581 | – |
| 16 | 0.021766 | 1.744 | 0.027744 | 1.759 | 0.060317 | 1.771 |
| 32 | 0.0059369 | 1.874 | 0.0074466 | 1.898 | 0.015659 | 1.946 |
| 64 | 0.0015461 | 1.941 | 0.0019227 | 1.953 | 0.0039513 | 1.987 |

### Distorted — INTERIOR (L2)

| mesh | scheme | L2 @8 | @16 | @32 | @64 | L2 order |
|---|---|---|---|---|---|---|
| 0.15h | uncorrected | 0.89217 | 0.48963 | 0.25319 | 0.1279 | 0.866, 0.951, 0.985 |
| 0.15h | **corrected (LS)** | 0.12234 | 0.031653 | 0.0079326 | 0.0019824 | **1.951, 1.996, 2.001** |
| 0.15h | corrected (GG) | 0.12155 | 0.031644 | 0.0079328 | 0.0019824 | 1.942, 1.996, 2.001 |
| 0.25h | uncorrected | 1.474 | 0.81431 | 0.42175 | 0.21313 | 0.856, 0.949, 0.985 |
| 0.25h | **corrected (LS)** | 0.12854 | 0.032222 | 0.0079784 | 0.0019856 | **1.996, 2.014, 2.007** |
| 0.45h | uncorrected | 2.6226 | 1.461 | 0.75845 | 0.38354 | 0.844, 0.946, 0.984 |
| 0.45h | **corrected (LS)** | 0.16702 | 0.034592 | 0.0081435 | 0.0019969 | **2.271, 2.087, 2.028** |

### Distorted — GLOBAL

| mesh | variant | L1 @64 (p) | L2 @64 (p) | L∞ @64 (p) |
|---|---|---|---|---|
| 0.15h | uncorrected | 0.098403 (0.982) | 0.12775 (0.983) | 0.30271 (0.992) |
| 0.15h | **corrected (LS)** | 0.0028482 (1.969) | 0.0082406 (1.553) | 0.073605 (1.032) |
| 0.45h | uncorrected | 0.29505 (0.980) | 0.38311 (0.982) | 0.9138 (1.001) |
| 0.45h | **corrected (LS)** | 0.0055129 (2.002) | 0.024124 (1.526) | 0.22414 (1.054) |

The uncorrected two-point operator is **inconsistent** on a non-orthogonal
mesh: first order even in the interior, with an error that grows with
distortion. The corrected operator is second order in the interior at
every distortion level, and its error is nearly independent of distortion.
The global L∞ and L2 orders are limited by the boundary ring (§16). Full
tables are in `focused_tests.log`.

---

## 15. Convergence results — summary

Order is measured between the two finest grids.

| Quantity | uncorrected | corrected |
|---|---|---|
| Diffusion operator, interior L2 order (0.15h / 0.25h / 0.45h) | 0.985 / 0.985 / 0.984 | **2.001 / 2.007 / 2.028** |
| Conduction solution L2 order, 0.45h | 1.077 | **2.004 (GG) / 2.013 (LS)** |
| Conduction solution L∞ order, 0.45h | 1.048 | 1.837 (GG) / 1.840 (LS) |
| Species solution L2 order, 0.45h | 1.043 | **2.037** |
| SIMPLE Couette velocity L2 order, 0.25h | 1.950 | **2.996** |
| Momentum corrector iteration (per pass) | — | ~30× contraction |
| GG skew sweeps (per sweep, linear field) | — | ~100–1000× contraction |

---

## 16. Boundary accuracy (Phase J)

**Finding.** On distorted meshes, the corrected operator's truncation error
is second order in the interior but **first order in the boundary-adjacent
ring (ring 0)**. The **solution** is nevertheless second order in L1/L2
(conduction 2.00, species 2.04), and its L∞ order approaches 2 (1.63 →
1.84). This is the classical supraconvergence of cell-centred FV schemes: an
O(h) truncation error confined to an O(h)-wide ring contributes O(h²) to
the solution error.

**Cause, measured** (`DiffusionTest.BoundaryRingTruncationIsFirstOrderAndLocalized`,
distorted 0.15h, LS, exact per-face Dirichlet):

| n | ring-0 max truncation error | ring-0 with **exact** boundary flux | interior max truncation error | max \|boundary flux error\|/V | max \|internal flux error\|/V (ring-0 cells) |
|---|---|---|---|---|---|
| 16 | 0.3175 | 0.0826 | 0.06181 | 0.3401 | 0.0801 |
| 32 | 0.1505 | 0.04062 | 0.01575 | 0.1553 | 0.04032 |
| 64 | 0.0736 | 0.02022 | 0.003956 | 0.07524 | 0.02018 |
| order | **1.077, 1.032** | **1.024, 1.006** | **1.973, 1.993** | ~1.1, 1.0 | ~1.0, 1.0 |

**Interpretation.**
- An individual face flux error divided by the cell volume is O(h) on every
  cell, interior included (column 6). In the interior, the errors of
  opposite faces are smooth and cancel to O(h²).
- In ring 0, the boundary face flux has a different error structure:
  - it comes from the pre-existing one-sided chain-fit `∂φ/∂n`, which
    assumes a cell chain normal to the boundary;
  - its non-orthogonal part uses the owner-cell gradient.
  It is about 4× larger than an internal face's error (column 5 vs 6), so
  nothing cancels.
- Even with an **exact** boundary flux, ring 0 stays first order. The
  leftover (0.0826 → 0.0202) equals the single uncancelled internal-face
  error (0.0801 → 0.0202). The first order is therefore structural: it comes
  from the loss of opposite-face cancellation at the boundary, not only from
  the pre-existing boundary formula.
- Making ring 0 second order would need a boundary flux whose error mirrors
  the opposite internal face's, a higher-order boundary reconstruction on
  distorted cells. That was **not** done. It is documented quantitatively
  here and its solution-level consequence is measured. The gate is that the
  solution converges at second order, which it does.

---

## 17. Conservation

| Test | Result |
|---|---|
| Diffusion face antisymmetry under face reversal, both gradient schemes | per-cell agreement ≤ 1e-10 relative |
| Constant field, distorted, corrected: Σ(∇·Γ∇φ)V | 0 to 1e-9 |
| Closed domain (all Neumann), distorted 10×10 0.4h: Σ(diff·V), corrected vs uncorrected | 0.030125142782893244 (GG), 0.03012514278289502 (LS) vs 0.030125142782892134 (equal to 3e-15; the value itself is pre-existing, see below) |
| Net Dirichlet boundary flux (exact 0), distorted 10×10 0.3h | uncorrected 0.0139, corrected **0.000521** |
| Momentum, Outlet everywhere: Σ RHS change from the correction | 0 to 1e-14 |
| Pressure correction: continuity with vs without explicit face terms (§7) | **5.24e-13** vs 0.0157 |
| SIMPLE distorted cavity: max cell imbalance / global net / wall flux (§9) | 7.87e-10 / 0 / 0 |
| CompressibleSIMPLE cavity: max cell imbalance, global, wall flux (§10) | 6.78e-11–1.54e-10 / 0 / 0 |
| CompressibleSIMPLE open channel: inlet vs outlet mass flow | −0.294156073 / +0.294156074, net 7.37e-10 |
| Thermal energy balance Σ(AT − b), every grid and variant | ≤ 7.1e-11 (Dirichlet), −1.39e-9 (prescribed-flux walls) |
| Species balance Σ(Ac − b) | ≤ 1.18e-9 |

**Pre-existing, not changed.** On a closed all-zero-gradient domain the
pre-existing boundary branch gives a nonzero net boundary flux on distorted
meshes (0.0301 above; −3.8e-15 on Cartesian). This is a property of the
chain-fit boundary reconstruction on distorted cells, and the correction
does not change it.

---

## 18. Degenerate geometry and robustness

| Case | Behavior |
|---|---|
| `Sf ⟂ d` (90°) | decomposition invalid; diffusion and pressure fall back to the finite two-point coupling for that face |
| coincident centroids (`\|d\| = 0`) | `MeshQuality` reports `valid=false` with finite metrics; pressure coupling throws `NumericalError` (SIMPLE reports `NonFiniteState`) |
| strong distortion (0.45h, max θ ≈ 11°) | all corrected values finite; SIMPLE and CompressibleSIMPLE converge |
| folded/negative cell | rejected at construction |
| **Pre-existing:** the uncorrected two-point diffusion divides by zero for `\|d\| = 0` | the correction's fallback is never worse |

---

## 19. Tests

### Stage 2 tests: 29 new, 1 renamed, 1 removed (net +28)

| Suite | Tests |
|---|---|
| `PressureCorrectionNonOrthogonalTest` | CartesianCompatibility, NonOrthogonalInternalFace, NonOrthogonalConservation, NonOrthogonalBoundary, DegenerateGeometryRejected |
| `SIMPLENonOrthogonalTest` | DistortedMeshMild, DistortedMeshModerate, DistortedMeshStrong, NonOrthogonalMassConservation, NonOrthogonalDeterministic, NonOrthogonalCorrectionPassCount, CorrectedSolveIsCloserToExactCouetteFlowOnDistortedMesh; **NonOrthogonalCorrectionsZeroPreservesBaseline** (renamed from stage 1's `ZeroCorrectionsPreservesBaselineAndCartesianIsUnaffected` and extended with the distorted N = 0 vs N = 1 check) |
| `CompressibleSIMPLENonOrthogonalTest` | CartesianCompatibility, NonOrthogonalPressureCorrection, NonOrthogonalOpenChannel, LowMachRegression |
| `SkewnessTest` | ProductionInterpolationUsesCorrection, CorrectedProductionOperatorImprovesError |
| `ThermalNonOrthogonalTest` | NonOrthogonalDiffusion, CartesianSolveIsBitIdentical, PrescribedFluxWallsKeepEnergyBalance |
| `SpeciesNonOrthogonalTest` | NonOrthogonalDiffusion, CartesianSolveIsBitIdentical |
| `TurbulenceNonOrthogonalTest` | NonOrthogonalDiffusion, ModelsRouteTheCorrectionThroughTheSharedOperator |
| `TurbulenceCaseTest` | NonOrthogonalCorrectionReachesEveryTurbulenceModel |
| `SIMPLESettingsTest` | NonOrthogonalOptionsFollowTheCorrectionCountAndGradientScheme |
| `DiffusionTest` | BoundaryCorrectionClassificationCoversEveryConditionType, BoundaryRingTruncationIsFirstOrderAndLocalized |

- **Removed:** `SIMPLENonOrthogonalTest.PressureCorrectionRejectsDistortedMeshKnownLimitation`.
  It pinned the axis-aligned-only throw, which no longer exists.
- **Updated (existing):**
  - `test_gradient.cpp`, `test_vector_gradient.cpp`, `test_diffusion.cpp`
    and `test_momentum_non_orthogonal.cpp`: two assertions that assumed
    plain GG is inexact on distorted meshes became three-way comparisons
    (plain GG, production GG, LS);
  - `test_turbulence_case.cpp`;
  - `test_simple_settings.cpp`.
- **Stage 1 tests:** 61, listed in the stage-1 record. They cover
  `MeshQuality`, `MeshGeometryNonOrthogonal`, `DiffusionTest`,
  `GridRefinementTest.Diffusion*`, `InterpolationTest`,
  `VectorGradientTest`, `MomentumNonOrthogonalTest`,
  `NonOrthogonalCorrectionPassesTest`, `SIMPLENonOrthogonalTest` and
  `CaseReader`/`CaseWriterTest`.

Mapping to the required names: `PressureCorrection.*` →
`PressureCorrectionNonOrthogonalTest.*`; `SIMPLE.*` →
`SIMPLENonOrthogonalTest.*`; `CompressibleSIMPLE.*` →
`CompressibleSIMPLENonOrthogonalTest.*`; `Skewness.*` → `SkewnessTest.*`;
`Thermal`, `Species` and `Turbulence.NonOrthogonalDiffusion` →
`{Thermal,Species,Turbulence}NonOrthogonalTest.NonOrthogonalDiffusion`.
The suite prefixes differ, but every required test name exists.

### Focused suites (`focused_tests.log`, final run): **412/412**

| Binary / filter | Passed |
|---|---|
| `CFDMeshTests` MeshQuality.\*:MeshGeometry\* | 18/18 |
| `CFDDiscretizationTests` Diffusion/GridRefinement(Diffusion, Laplacian)/Interpolation/VectorGradient/Gradient/Skewness | 55/55 |
| `CFDPhysicsTests` MomentumNonOrthogonal/MomentumDiffusion/MomentumVariableViscosity | 17/17 |
| `CFDSimpleTests` PressureCorrection\*/SIMPLENonOrthogonal/NonOrthogonalCorrectionPasses/SIMPLESettings | 39/39 |
| `CFDCompressibleSimpleTests` (all) | 23/23 |
| `CFDThermalTests` (all) | 83/83 |
| `CFDSpeciesTests` (all) | 37/37 |
| `CFDTurbulenceTests` (all) | 134/134 |
| `CFDIoTests` \*NonOrthogonal\*/CaseWriter round trip | 6/6 |

---

## 20. Final regression

- **Command:** `ctest -j32 --output-on-failure` (WSL2, `build/debug`), run
  after the final build of every source and test change. The only later
  edit is one comment line in `SIMPLE.cpp`, see below.
- **Result: 1506/1506 passed, 0 failed.** `ctest -N` lists 1519 tests; the
  other 13 are the pre-existing `DISABLED_` tests. The set is the same 13 as
  the baseline (name-by-name diff: identical). Wall time 706.3 s.
- **Accounting against the stage-1 baseline (1478 passed, 1491 listed), by
  test-name diff of the two ctest logs:**
  - 30 names added: the 29 new tests plus the renamed test, all listed in
    §19;
  - 2 names gone: the renamed test's old name, and the obsolete
    `PressureCorrectionRejectsDistortedMeshKnownLimitation`;
  - net: 1478 + 29 − 1 = **1506**.
  Every added test is a NUM-003 test.
- **An earlier full run** in this stage (before the Phase J test, the
  rename, and the added measurement printouts) was 1505/1505 of 1518 (707.9
  s), with the same accounting minus those changes.
- **Validation-file noise:** each full run rewrote the same 13 tracked
  `results/validation/**/validation.json` files (natural convection Ra1e3 ×7;
  channel flow k-ε/k-ω/SST coarse+medium ×6). `git diff` showed the only
  changed field in all 13 was `"runtime_seconds"`: 26 changed lines, 0 lines
  outside that field. They were reverted with `git checkout` after each run.
  No numerical validation value changed.
- **After the run:** one comment line in `src/pressure_velocity/SIMPLE.cpp`
  was corrected. It had said the pressure passes were "warm-started"; they
  use a zero initial guess, as the code and §8 state. The file was rebuilt
  cleanly. The change is comment-only, so the run above remains valid for
  the final code.

---

## 21. Acceptance gates

| Gate | Result |
|---|---|
| **A.** Pressure correction geometric (not a deleted guard), one implementation for incompressible and compressible, boundaries deliberate, Cartesian equivalence | **PASS** (§5, §6): shared `assembleGeometricPressureCorrection`; Dirichlet/Neumann faces pinned; bit-identical Cartesian library dump |
| **A.** Face-flux consistency | **PASS** (§7): 5.24e-13 vs 0.0157 without the explicit terms; no Rhie-Chow exists to keep (pre-existing) |
| **B.** Distorted SIMPLE mild/moderate/strong: finite, continuity, mass closure, determinism, analytical flow | **PASS** (§9): 12 runs converge; imbalance ≤ 1e-9; wall flux 0; bit-identical repeat; Couette order 1.95 → 3.00 |
| **C.** `non_orthogonal_corrections`: 0 = baseline, N > 0 = exactly N passes | **PASS** (§5, §8) |
| **D.** CompressibleSIMPLE: same principles, low-Mach, Arkilic, EOS, distorted test | **PASS** (§10): low-Mach on distorted to 7.75e-10; Arkilic passes; cavity and channel conserve mass |
| **E.** Skewness in production operators, one implementation, quantitative improvement | **PASS** (§11): production GG error 0.0876 → 1.13e-9 (0.45h); propagates to every GG consumer |
| **F/G/H.** Thermal/species/turbulence via the shared correction, verified | **PASS** (§12): conduction L2 order 1.08 → 2.00, energy balance ≤ 1.4e-9; species 1.04 → 2.04; turbulence operator 2.4e-4 → 2e-14 |
| **I.** Gradient choice configured with an honest GG note; both schemes tested | **PASS** (§13) |
| **J.** First-order boundary behavior: cause found, fixed or quantitatively documented | **PASS (documented)** (§16): mechanism measured per face type; solution second order |
| Cartesian results unchanged | **PASS** (§5) |
| Full regression; count increase attributable to NUM-003 tests | **PASS** (§20) |

---

## 22. Remaining limitations

1. **No production mesh is non-orthogonal yet.** `CaseBuilder` builds only
   structured Cartesian meshes, where every correction is exactly zero, so
   `non_orthogonal_corrections` has no effect on any case file today.
   Distorted meshes are reachable through the library API and tests only
   (`DistortedMesh.hpp` is test-only). Non-orthogonal mesh import or
   generation is P12-MESH.
2. **No Rhie-Chow interpolation** (pre-existing). Face flux is
   `ρ interp(u*)·Sf` plus the SIMPLE correction. Consistency of the
   correction step is preserved (§7), but pressure–velocity decoupling on
   collocated grids is only as well controlled as before this task.
3. **PISO / transient.** PISO uses the geometric pressure coupling (two-
   point; it no longer throws on distorted meshes) but has **no**
   non-orthogonal pass loop. `TransientMomentum`'s viscous term is **not**
   corrected. Neither is wired to `non_orthogonal_corrections`.
4. **Conjugate `ThermalInterface`** keeps its own inline two-point
   coefficient and is not corrected.
5. **Boundary ring** truncation error is first order on distorted meshes
   (§16). The solution is second order in L1/L2, and its L∞ order is
   1.63 → 1.84 on the grids measured.
6. **Pre-existing BiCGSTAB breakdowns** (P12-NUM-004 scope, not touched):
   - the absolute breakdown thresholds (`constants::tiny`) trip on
     warm-started solves at small residual scales, hence the zero-guess pass
     solves (§8);
   - in exploratory scratch runs the unpreconditioned pressure solve also
     broke down on some Couette runs, including on the untouched Cartesian
     path, so the committed Couette test uses the existing Jacobi
     preconditioner option.
7. **Not skew-corrected:** the predictor mass flux, Central/QUICK face
   values and the diffusion face gradient (§11).
8. **Turbulence** is verified at operator level plus wiring. There is no
   turbulent-flow solution study on a distorted mesh (P12-TURB).
9. **Pre-existing, unchanged:** the chain-fit boundary reconstruction's
   nonzero Neumann net flux on distorted cells (§17); the two-point
   formula's division by zero at `|d| = 0`; `MeshQuality`'s
   Cartesian-specific aspect ratio.
10. **No GUI exposure** of `non_orthogonal_corrections` (consistent with
    P12-NUM-001/002).
11. **The thermal prescribed-flux test's outer-iteration cap is 3000** (§12),
    justified by the measured pre-existing Picard contraction, which is
    identical with and without the correction.
