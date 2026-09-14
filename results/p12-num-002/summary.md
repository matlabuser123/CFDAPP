# P12-NUM-002 — Gradient Reconstruction

**Status:** COMPLETE (within the scope decided below).
**Scope:** one authoritative `GradientScheme` abstraction (GreenGauss/
LeastSquares) wired into the actual production pressure-gradient path
(`cfd::physics::assemblePressureSourceContribution`,
`cfd::pressure_velocity::correctVelocity`), selectable via `solver.json`'s
`gradient_scheme`, defaulting to `green_gauss` (byte-identical to every
pre-existing case). A new, narrowly-scoped deterministic distorted-quad
mesh test capability was added to make distorted-mesh verification
possible at all (previously structurally blocked). Turbulence's SST
cross-diffusion gradients and P12-NUM-001's own LinearUpwind convection
gradient are **not** wired to this selector in this task — see
Limitations.

**Baseline (before this task):** `GridRefinementTest.
GradientOfSmoothFieldConvergesAtSecondOrder` passing, observed order
1.91/1.96/1.98 (8→16→32→64) on a Cartesian mesh — unchanged after this
task (see §4).

---

## 1. Audit of the existing implementation

`cfd::discretization::gradient(mesh, field, boundaries)` (Green-Gauss,
`include/cfd/discretization/Gradient.{hpp,cpp}`) computes
`grad(phi)_P = (1/V_P) sum_f phi_f * Sf_cell`, with an existing boundary
refinement (`tryPairedBoundaryContribution`): where an interior neighbor
sits directly opposite a boundary face (true for every non-degenerate
Cartesian cell), it replaces the plain {boundary, opposite} face pair
with an exact quadratic-fit one-sided derivative — 2nd order, not the 1st
order a plain mixed exact-boundary/interpolated-interior sum would give.

Production callers of this scalar operator (found by direct grep, not
assumed): `MomentumEquation.cpp`'s pressure-source term,
`PressureCorrectionEquation.cpp`'s velocity-correction step,
`SSTModel.cpp`'s k/omega cross-diffusion term, and P12-NUM-001's own
`Convection.cpp` (LinearUpwind's per-cell gradient). A *separate*,
pre-existing vector-valued implementation, `computeVelocityGradient`
(`VectorGradient.{hpp,cpp}`), is used for velocity gradients specifically
(turbulent production in `KEpsilonModel`/`KOmegaModel`/`SSTModel`, and
P12-NUM-001's own velocity-component LinearUpwind path in
`MomentumEquation.cpp`) — deliberately not the plain Green-Gauss's
boundary refinement, since it needs vector BCs, not scalar ones (already
documented in that file before this task). Not touched here (see
Limitations).

No least-squares gradient existed anywhere (`grep -ri leastsquares`
across `src/` found nothing, confirmed again after implementation that
the only hits are this task's own new code).

---

## 2. GradientScheme abstraction

`include/cfd/discretization/Gradient.hpp` gained:

- `enum class GradientScheme { GreenGauss, LeastSquares }`
- `parseGradientScheme(std::string_view)` / `gradientSchemeName(...)` —
  mirrors `parseConvectionScheme`'s own convention exactly.
- `gradient(mesh, field, boundaries, scheme = GradientScheme::GreenGauss)`
  — the existing function, **unchanged Green-Gauss code path**, gated by
  an `if (scheme == LeastSquares) return leastSquaresGradient(...)` at
  the very top. Every existing call site (which never passes `scheme`)
  is byte-identical — confirmed by `GradientTest.
  DefaultSchemeArgumentMatchesExplicitGreenGauss` and by the unchanged
  `GradientOfSmoothFieldConvergesAtSecondOrder` numbers in §4.

---

## 3. Least-squares gradient formulation

For cell P, one `(displacement, valueDifference)` pair is gathered per
face: `(r_neighbor - r_P, phi_neighbor - phi_P)` for an internal face;
`(r_faceCentroid - r_P, bc.boundaryValue(phi_P, distance) - phi_P)` for a
boundary face (the assigned condition's own value at the face's own true
position — no ghost-mirror/offset trick needed, unlike Convection.cpp's
upwind boundary treatment, because least-squares only needs a (position,
value) pair, and the boundary face's own centroid already is one).

**Weighting:** `w_i = 1/|displacement_i|^2` (inverse-distance-squared) —
closer neighbors, whose finite-difference estimate of the local slope is
more locally representative, are trusted more. A standard, documented
choice (matches common unstructured-CFD practice, e.g. OpenFOAM's
`leastSquares` scheme).

**System solved** (`solveLeastSquaresGradient`, a pure primitive, no
Mesh/Field dependency): the 2x2 weighted normal equations

```
[Sxx Sxy] [gx]   [bx]      Sxx = sum w_i dx_i^2      bx = sum w_i dx_i dphi_i
[Sxy Syy] [gy] = [by]      Syy = sum w_i dy_i^2      by = sum w_i dy_i dphi_i
                           Sxy = sum w_i dx_i dy_i
```

solved directly via Cramer's rule (no external linear-algebra
dependency, per the task's own preference for a small explicit 2x2 solve
in this 2D solver).

**Conditioning:** `wellConditioned = false` (gradient left as a harmless
`{0,0}` placeholder, never NaN/Inf) whenever
`determinant = Sxx*Syy - Sxy^2 < 1e-10 * (Sxx+Syy)^2` — a scale-invariant
threshold (both sides have the same units, so the ratio is dimensionless
regardless of mesh spacing/field magnitude), the geometric signature of
every contributing displacement being (near-)colinear. A defensive
`std::isfinite` check backstops this (never reachable given the
conditioning guard, but kept per "never produce NaN/Inf silently").

**Fallback policy (deterministic, tested):** any cell whose local system
is not well-conditioned uses that cell's Green-Gauss value instead
(computed once for the whole mesh up front in `leastSquaresGradient`).

---

## 4. Green-Gauss: preserved, re-verified

Unchanged code path (§2) — re-ran after all P12-NUM-002 changes:

| Test | Result |
|---|---|
| `GradientTest.ConstantFieldHasZeroGradient` | PASS |
| `GradientTest.PhiXGivesGradientOneZero` | PASS |
| `GradientTest.PhiYGivesGradientZeroOne` | PASS |
| `GradientTest.QuadraticFieldMatchesAnalyticalGradient` | PASS (error < 1e-9) |
| `GridRefinementTest.GradientOfSmoothFieldConvergesAtSecondOrder` | PASS, orders 1.913/1.962/1.982 — **identical to the pre-task baseline**, confirming zero regression |

---

## 5. Least-squares results

### 5.1 Exactness (constant / linear fields)

| Test scenario | Max error |
|---|---|
| Constant field, Cartesian | 0 (exact) |
| Linear field (`2x+3y+5`), Cartesian, every cell incl. boundary-adjacent | 0 (machine-precision, < 1e-9 asserted) |
| Linear field, **distorted** mesh (0.3·h amplitude), every cell | 7.55e-15 |
| Green-Gauss, same distorted mesh, same linear field (for contrast) | **4.90e-2** |

This is the headline distorted-mesh finding: least-squares reconstructs
a linear field's gradient **exactly regardless of mesh distortion** (a
residual of exactly zero is achievable for any weighting when every data
point satisfies the plane equation exactly); Green-Gauss has a genuine,
non-vanishing error there (needs the non-orthogonal correction that is
P12-NUM-003, explicitly out of scope) — measured, not assumed.
(`LeastSquaresTest.LinearFieldDistortedIsExactAtEveryCell`,
`LeastSquaresTest.GreenGaussDistortedIsNotExactUnlikeLeastSquares`.)

### 5.2 Smooth-field convergence (manufactured `phi = sin(pi x) cos(pi y)`, grids 8/16/32/64)

Two L2 norms are measured per scheme/mesh, for the same reason (and with
the same interpretation) as P12-NUM-001's convection studies: boundary
faces (and the boundary-adjacent ring) carry a real but bounded
truncation term that dominates the *global* rate, while the *interior*
rate isolates the scheme's own genuine accuracy.

**GLOBAL (whole domain):**

| Scheme / mesh | 8→16 | 16→32 | 32→64 |
|---|---|---|---|
| Green-Gauss / Cartesian (pre-existing, unchanged) | 1.913 | 1.962 | 1.982 |
| Least-squares / Cartesian | 1.663 | 1.622 | 1.577 |
| Green-Gauss / Distorted (0.15·h amplitude) | 1.720 | 1.639 | 1.580 |
| Least-squares / Distorted | 1.688 | 1.632 | 1.579 |

**INTERIOR-ONLY (excluding the boundary-adjacent ring):**

| Scheme / mesh | 8→16 | 16→32 | 32→64 |
|---|---|---|---|
| Green-Gauss / Cartesian | 2.043 | 2.009 | 2.002 |
| Least-squares / Cartesian | 2.043 | 2.009 | 2.002 |
| Green-Gauss / Distorted | 2.179 | 2.054 | 2.013 |
| Least-squares / Distorted | 2.104 | 2.030 | 2.007 |

Notable, mathematically explicable findings (not bugs):

- **Green-Gauss and least-squares are numerically identical in the
  Cartesian interior** — on a uniform grid, the standard 4-neighbor
  (N/S/E/W) least-squares gradient with inverse-distance-squared
  weighting reduces exactly to the central-difference (Green-Gauss)
  formula for a symmetric isotropic stencil, a known equivalence.
- Both schemes reach ~2.0 in the interior on **either** mesh — the
  genuine underlying accuracy is 2nd order regardless of distortion.
- Green-Gauss's *global* Cartesian rate (~1.9-2.0, from the pre-existing
  test) is noticeably better than its own *global* distorted rate
  (~1.58-1.72): its boundary refinement's assumption (paired boundary/
  opposite face areas match) is exactly true on an orthogonal Cartesian
  mesh and only approximately true once distorted, so it loses its
  Cartesian-specific boundary advantage there — converging to the same
  ~1.6-1.7 global range least-squares already has on both meshes.

L1/Linf were also recorded during development (see the session's own
diagnostic output); L2 is the metric asserted in the committed tests, per
this repository's existing `GridRefinementTest` convention.

### 5.3 Degeneracy / conditioning

Real mesh cells from this codebase's only generators (Cartesian, or the
new distorted-quad one) always contribute displacements spanning both
the x and y directions (every cell has exactly 2 x-facing and 2 y-facing
faces, each either an internal neighbor or a boundary virtual-neighbor)
— **genuine local-system degeneracy is structurally unreachable from an
actual produced mesh**, confirmed by inspection, not merely untested.
The detection/fallback machinery is nonetheless fully implemented and
tested directly at the `solveLeastSquaresGradient` primitive level with
synthetic inputs: an exactly colinear stencil, a single-neighbor
stencil, a zero-neighbor stencil, and a *nearly* (not exactly) colinear
stencil are all correctly flagged `wellConditioned = false`; a
zero-distance entry is skipped rather than dividing by zero; a broad
scale sweep (1e-6 to 1e6) never produces a non-finite accepted result.

---

## 6. Distorted-mesh test capability (new, test-only)

`tests/unit/discretization/DistortedMesh.hpp`:
`createDistortedQuad2D(nx, ny, lengthX, lengthY, distortionAmplitude)`.
Builds an explicit `(nx+1)x(ny+1)` logical vertex grid, perturbs each
interior vertex by
`amplitude * sin(pi*x/Lx) * sin(pi*y/Ly)` (x) and
`amplitude * sin(2*pi*x/Lx) * sin(pi*y/Ly)` (y) — deterministic, no RNG,
and identically zero on all four domain edges (any `sin(...)` factor
vanishes there), so the outer rectangular domain is exactly preserved
regardless of amplitude. Each cell's true polygon centroid/volume (2D
shoelace formulas) and each face's true centroid/area-vector (edge
midpoint / rotated edge vector, oriented to reduce exactly to
`MeshGeometry::createCartesian2D`'s own convention in the undistorted
limit) are computed from the perturbed vertices directly — ordinary
polygon geometry, no new mesh framework.

Verified (ad hoc diagnostic, then exercised implicitly by every
distorted-mesh test above): total mesh volume matches the domain area
exactly; per-cell face-vector closure (divergence theorem for a closed
polygon) holds to 1e-9 for every cell; boundary face centroids sit
exactly on the domain edges; distortion factors from 0.15 to 1.5 (times
`min(dx,dy)`) all produce valid (positive-area) meshes — the self-
intersecting-cell guard (`InvalidArgumentError`) exists and is
documented but was not naturally triggered by this generator's own
smooth perturbation at any tested amplitude (a defensive check, not
untested logic — it is simple, direct positive-area validation, not
complex enough to need its own synthetic-failure unit test).
`cfd::test::perFaceBoundaryMesh` (used throughout P12-NUM-001 for exact
per-face boundary values) was generalized with a `const Mesh&` overload
so it works on this distorted mesh unchanged (the `nx,ny,lengthX,lengthY`
overload now just forwards to it — byte-identical for every existing
P12-NUM-001 caller).

---

## 7. Boundary treatment

Both schemes have an explicit, deliberate boundary treatment (never
"ignored"):

- **Green-Gauss**: the pre-existing paired-boundary quadratic fit
  (unchanged, §1/§4).
- **Least-squares**: the assigned `ScalarBoundaryCondition`'s own value
  at the boundary face's own true centroid, used as one more
  `(position, value)` data point in the same weighted system as every
  interior neighbor — proven exact for a linear field at every boundary-
  adjacent cell (§5.1), and empirically close to 2nd order for a smooth
  field once the boundary-adjacent ring's own (real, bounded, non-
  vanishing-under-refinement-at-the-SAME-rate-as-the-interior)
  contribution is excluded (§5.2's global-vs-interior split) — the exact
  same phenomenon P12-NUM-001 documented for convection's own boundary
  treatment, not a new or different limitation.

---

## 8. Production integration

`cfd::pressure_velocity::SIMPLESettings` gained `gradientScheme`
(default `GreenGauss`). Threaded through the two `gradient()` call sites
inside the plain incompressible SIMPLE loop:

- `assemblePressureSourceContribution` (`MomentumEquation.{hpp,cpp}`) —
  called by `assembleRelaxedMomentumComponent`
  (`RelaxedMomentum.{hpp,cpp}`, SIMPLE's own per-outer-iteration momentum
  assembly), which now forwards `gradientScheme` through unchanged.
- `correctVelocity` (`PressureCorrectionEquation.{hpp,cpp}`) — called
  directly by `SIMPLE.cpp`'s own correction step.
- `SIMPLE.cpp` passes `settings_.gradientScheme` to both.

All four added parameters default to `GreenGauss`, so every pre-existing
call site (PISO/`TransientMomentum.cpp`, compressible's
`CompressibleMomentum`/`CompressibleRelaxedMomentum`/`CompressibleSIMPLE`,
and every test that does not explicitly pass a scheme) is byte-identical
— confirmed by the full regression (§10) and by
`MomentumPressureTest.DefaultGradientSchemeArgumentMatchesExplicitGreenGauss`/
`VelocityCorrectionTest.DefaultGradientSchemeArgumentMatchesExplicitGreenGauss`.

`solver.json`'s new `gradient_scheme` (optional, default `"green_gauss"`,
values `"green_gauss"`/`"least_squares"`, invalid values throw
`CaseConfigurationError`) round-trips through `CaseWriter` and maps to
the enum via `cfd::discretization::parseGradientScheme` in
`CaseBuilder.cpp` — mirrors `convection_scheme`'s own P12-NUM-001
pattern exactly.

---

## 9. Focused test results (all passing)

- `CFDDiscretizationTests`, gradient-related filter (`GradientTest.*`,
  `GradientSchemeTest.*`, `SolveLeastSquaresGradientTest.*`,
  `LeastSquaresTest.*`, `GridRefinementTest.*Gradient*`): **33/33**.
- Full `CFDDiscretizationTests` target: **97/97** (up from 69 after
  P12-NUM-001).
- `CFDPhysicsTests`, `MomentumPressureTest.*`: **7/7** (2 new: default-
  argument equivalence; least-squares wiring/exactness at the production
  call site).
- `CFDSimpleTests`, `VelocityCorrectionTest.*`: **6/6** (2 new: same
  pair, for `correctVelocity`).
- `CFDIoTests`, gradient-scheme config parsing/round-trip: **4/4** new
  (missing→default, both valid names, invalid value throws) + 1 extended
  round-trip test + 1 new non-default round-trip test.
- Full `CFDIoTests` target: **208/208** (up from 204).
- Full `CFDSimpleTests`/`CFDPisoTests`/`CFDCompressibleSimpleTests`:
  77/77, 68/68, 19/19 — unchanged, confirming the production wiring
  changes did not disturb PISO/compressible paths (which stay on the
  GreenGauss default, never passing a scheme).

## 10. Full regression

`ctest -j32` (WSL/Linux `build/debug`): **1417/1417 passed, 0 failed**
(up from 1381/1381 pre-task, net +36 new tests actually executed), 13
validation tests intentionally `DISABLED_` (unchanged count from before
this task, pre-existing/unrelated). Every tracked
`results/validation/**/validation.json` file that regenerated during
this run changed only its own `runtime_seconds` field (confirmed by
diff, not assumed) — reverted per this repository's own git-discipline
rule, exactly as done for P12-NUM-001.

---

## 11. Limitations (disclosed, not gaps)

- **Turbulence (SST cross-diffusion)**: `SSTModel.cpp`'s own two
  `gradient()` calls (for k and omega) are not wired to `gradientScheme`
  — audited (task requirement 11), found to be a real consumer, but
  extending case-level configuration into `TurbulenceModel`
  construction/`TurbulenceModelFactory` is a materially larger change
  than this task's "smallest coherent change" scope; unaffected (still
  GreenGauss, unchanged) and verified via the full regression's
  turbulence suites passing unchanged.
- **P12-NUM-001's own LinearUpwind convection gradient**: `Convection.cpp`
  calls the plain 3-argument `gradient(mesh, field, boundaries)` inside
  `convection()` (scalar path) and `MomentumEquation.cpp`'s convection
  assembly separately calls `computeVelocityGradient` (velocity-component
  path) — neither is wired to `GradientScheme` in this task, deliberately
  (conflating two independent config knobs, i.e. "which convection
  scheme" and "which gradient scheme used *inside* that convection
  scheme", was judged more confusing than useful without a concrete need
  driving it). Verified NOT regressed: P12-NUM-001's own full test suite
  (`CFDDiscretizationTests`'s convection filter, `MomentumConvectionTest.*`)
  re-passes unchanged in §9/§10.
- **`computeVelocityGradient` (VectorGradient.cpp)**: the separate,
  pre-existing vector-valued Green-Gauss implementation used by
  turbulent production (`KEpsilonModel`/`KOmegaModel`/`SSTModel`) and by
  P12-NUM-001's velocity-component LinearUpwind — not given a
  least-squares variant in this task (would double the implementation
  surface for a consumer this task's scope does not require).
- **Compressible/PISO paths**: `CompressibleMomentum`/
  `CompressibleRelaxedMomentum`/`CompressibleSIMPLE` and
  `TransientMomentum`/PISO's own `correctVelocity` calls stay on the
  `GreenGauss` default — not wired to a case-level selector (same
  "smallest coherent change, only the plain incompressible SIMPLE path"
  scoping decision P12-NUM-001 made for `convection_scheme`).
- **GUI**: `gradient_scheme` is not exposed in
  `apps/gui/CaseModelAdapter.cpp` / the Solver settings QML editor —
  same established precedent as `convection_scheme`
  (`LinearSolverSpec::backend`, P6-GPU-002) being case-file-only.
- **Non-orthogonal/skewed-mesh correction (P12-NUM-003)**: explicitly out
  of scope. Least-squares' distorted-mesh exactness/accuracy (§5) is a
  genuine, independent way to get good gradients on a distorted mesh
  without that correction — not a substitute for it, since P12-NUM-003
  targets the *diffusion* operator's own face coefficient, a different
  discretization concern entirely.
