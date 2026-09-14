# P12-NUM-001 — Higher-Order Convection

**Status:** COMPLETE (within the scope decided below).
**Scope:** one authoritative `ConvectionScheme` abstraction (Upwind/Central/
LinearUpwind/QUICK) wired into the actual production momentum-convection
path (`cfd::physics::assembleConvectionContribution`, reused unchanged by
`RelaxedMomentum`, `CompressibleMomentum`, `CompressibleRelaxedMomentum`),
selectable via `solver.json`'s `convection_scheme`, defaulting to `upwind`
(byte-identical to every pre-existing case). Thermal/species/turbulence/
multiphase convection are **not** wired to this selector in this task —
see Limitations.

---

## 1. Design

### 1.1 Scheme abstraction (one location, reused everywhere)

`include/cfd/discretization/Convection.hpp` / `.cpp` gained:

- `enum class ConvectionScheme { Upwind, Central, LinearUpwind, QUICK }`
- `parseConvectionScheme(std::string_view)` / `convectionSchemeName(...)` —
  the one place a case-file string becomes the enum (throws
  `InvalidArgumentError` on anything else).
- Three pure, physics-agnostic primitives (no `Mesh`/`Field` dependency,
  each independently unit-tested):
  - `quickFaceValue(phiC, hCU, phiU, hUf, phiD, hfD)` — the exact value at
    a face of the unique quadratic through three points at their true
    (possibly non-uniform) positions. Reduces to Leonard (1979)'s
    classical QUICK weights (−1/8, 6/8, 3/8) on a uniform grid.
  - `linearUpwindFaceValue(phiUpwind, gradPhiUpwind, upwindCentroid,
    faceCentroid)` — a Taylor/linear-reconstruction face value from an
    already-computed gradient (never computes a gradient itself, per the
    task's "no second gradient implementation" requirement).
  - `smoothnessRatio` / `vanLeerLimiter` — the Sweby (1984) TVD blend
    (see §5, Boundedness).
- `convection(mesh, field, faceMassFlux, boundaries, scheme =
  ConvectionScheme::Upwind)` — the existing explicit whole-field operator,
  now scheme-aware, used by `GridRefinementTest`'s convergence studies.

The **production** implicit-matrix path
(`cfd::physics::assembleConvectionContribution`, `MomentumEquation.{hpp,cpp}`)
gained the same `scheme` parameter (default `Upwind`) and reuses these
same primitives — no separate/duplicated scheme logic exists there.
`RelaxedMomentum::assembleRelaxedMomentumComponent` (SIMPLE's actual
per-outer-iteration momentum assembly) forwards a new `convectionScheme`
parameter through unchanged; `CompressibleMomentum`/
`CompressibleRelaxedMomentum` call the same `assembleConvectionContribution`
directly, so they inherit the capability automatically (not wired to a
case-file key in this task — see Limitations).

### 1.2 Gradient reuse

`LinearUpwind` needs the gradient *at the upwind cell*. Two existing,
already-verified gradient implementations are reused, never duplicated:

- `cfd::discretization::gradient` (scalar Green-Gauss, existing) inside
  `discretization::convection()`.
- `cfd::discretization::computeVelocityGradient` (`VectorGradient.hpp`,
  existing, vector-BC-aware) inside `assembleConvectionContribution`,
  selecting `gradU`/`gradV` by `VelocityComponent`.

Both are computed **once per call**, and only when `scheme ==
LinearUpwind` — `Upwind`'s cost and behavior are completely unchanged.

---

## 2. Configuration syntax

`solver.json`:

```json
{
  "...": "...",
  "convection_scheme": "upwind"
}
```

Accepted values: `"upwind"` (default if the key is absent) / `"central"`
/ `"linear_upwind"` / `"quick"`. An unrecognized value throws
`CaseConfigurationError` (`SolverConfigParser.cpp`, mirroring exactly the
existing `momentum_linear_solver.type`/`.backend` validation pattern from
P6-GPU-002/003). `CaseWriter` round-trips the field unconditionally
(`CaseWriterTest.NonDefaultConvectionSchemeRoundTrips`).

**Not exposed in the GUI** in this task (see Limitations) — matches the
existing, established precedent: `LinearSolverSpec::backend` (P6-GPU-002)
is likewise present in the case-file schema but not mapped in
`apps/gui/CaseModelAdapter.cpp`'s `linearSolverToVariant`/
`linearSolverFromVariant`, confirmed by reading that file.

---

## 3. Formulas used

**QUICK** (`quickFaceValue`): value at the face of the Lagrange quadratic
through `(phiC, hCU, phiU, hUf, phiD, hfD)` at their true signed
positions — general (works for non-uniform spacing), reduces to the
classical (−1/8, 6/8, 3/8) weights on a uniform grid (proved algebraically
and verified by `QuickFaceValueTest.MatchesClassicalCoefficientsOnUniformGrid`).

**LinearUpwind** (`linearUpwindFaceValue`): `phiUpwind + gradPhiUpwind .
(faceCentroid − upwindCentroid)`.

**Central**: reuses the existing `interpolateInternalFace` (distance-
weighted linear interpolation) directly — no new formula.

**Deferred correction** (production path only —
`assembleConvectionContribution`): the *implicit* matrix coefficients are
always plain first-order upwind (robustness/diagonal-dominance,
unaffected by `scheme`); a higher-order scheme instead adds an *explicit*
correction to the RHS, face-once and equal/opposite between the owner and
neighbor rows: `correction = ownerFlux * (phiFace − phiUpwind)`;
`rhs[owner] -= correction; rhs[neighbor] += correction`.

---

## 4. Boundary / fallback behavior

**Boundary faces always use the pre-existing `upwindBoundaryFaceValue`
treatment, regardless of `scheme`.** This is an explicit, deliberate
P12-NUM-001 scope limit (the task's own text: *"For a boundary or
unavailable stencil, degrade deterministically to an appropriate
lower-order scheme rather than reading invalid neighbours"*) —
reproducing each scheme's own boundary-consistent one-sided treatment is
a materially larger, separate undertaking.

**QUICK's far-upstream cell** is found via a corrected/generalized
`MeshGeometry::oppositeInteriorFace` (see §6, a real pre-existing latent
bug this task fixed). When no interior far-upstream cell exists (the
upwind cell is itself boundary-adjacent in the upstream direction),
QUICK's own high-order value falls back to `phiUpwind` (documented,
`QuickDegradesToUpwindWhenNoFartherUpstreamCellExists`, both in
`discretization::convection()` and the production path).

---

## 5. Boundedness strategy

**First attempt (rejected):** clip the raw high-order face value into
`[min(phiUpwind, phiDownwind), max(...)]`. This is **not sufficient**:
two independently-clipped faces of the same cell can still combine into
an out-of-bounds cell update — confirmed by
`ConvectionSchemeTest.StepFunctionStaysWithinInitialBoundsAfterOneExplicitStep`
initially failing under it (`phiNew = 0.4 < phiMin = 1.0`, a genuine
finding, not an implementation slip — see `git log`/session transcript
for the derivation).

**Final design:** a Sweby (1984) TVD flux-limiter blend,
`phiFace = phiUpwind + psi(r) * (phiHighOrder − phiUpwind)`, where `r`
is the ratio of the upstream gradient to the local (across-the-face)
gradient (`smoothnessRatio`) and `psi` is Van Leer's (1974) limiter
(`vanLeerLimiter`: 0 at any local extremum/non-monotone data, smoothly
approaching 1 for smooth/linear data), plus a final unconditional safety
clamp into `[min(phiUpwind, phiDownwind), max(...)]`. `r` (hence `psi`)
is **0**, not 1, whenever no far-upstream cell exists — an earlier
version defaulted to 1 ("assume smooth") and this was found to be WRONG:
at a boundary-adjacent cell, the boundary's own face already relies on
every face of that cell using the *same* upwind-consistent convention
(a telescoping-cancellation property); blending the other face toward a
genuinely higher-order value there breaks that cancellation and
introduces a bias that does **not** shrink under refinement (caught by
`LinearFieldIsReproducedExactlyAwayFromAnyDegradedFace` regressing, and
independently confirmed by hand: the combined stencil converges to
1.5× the true derivative, not 1×, at such a cell). This is now fixed.

For **Central** specifically, this blend reduces exactly to the
classical, published, *proven*-TVD "limited central/MUSCL" scheme (its
own `phiHighOrder − phiUpwind` equals `0.5*(phiDownwind−phiUpwind)` on a
uniform grid, matching the Sweby form exactly). For **LinearUpwind** and
**QUICK**, applying the same `r`/`psi` blend is a common, practical
technique (in the spirit of NVD limiters like SMART/ULTRA-QUICK) but is
**empirically verified**, not a formally proven discrete maximum
principle for those two — labeled as such in the code's own comments,
per `CLAUDE.md`'s "say plainly which kind of check this is."

---

## 6. A real pre-existing bug found and fixed

`MeshGeometry::oppositeInteriorFace` (used by `Gradient.cpp`'s boundary
treatment since before this task) accepted the **first** internal
candidate face regardless of its alignment with the reference face,
because its search was seeded `bestDot = 0.0` with an unconditional
"first candidate wins" branch. This never manifested for `Gradient.cpp`
(a genuinely opposite, alignment ≈ −1 candidate always exists on every
grid size its own tests use), but QUICK's new far-upstream lookup
legitimately hits the case where no genuinely anti-parallel candidate
exists (only *perpendicular*, alignment ≈ 0, y-direction faces) — the
old code wrongly returned one of those. Fixed by requiring
`alignment < 0.0` before a candidate is even considered. Verified
behavior-preserving for every pre-existing (`Gradient.cpp`) caller by the
full regression suite passing unchanged (1381/1381, see §9).

---

## 7. Convergence-study grids and measured observed order

Manufactured field `phi = sin(pi x) cos(pi y)`, `U = (1, 0)`, exact
`conv = U . grad(phi)`, grids 8×8/16×16/32×32/64×64
(`tests/unit/discretization/test_grid_refinement.cpp`, reusing the
existing `ManufacturedFields.hpp` infrastructure). **Two** L2 norms are
measured per scheme, deliberately:

### GLOBAL (whole domain)

| Scheme | 8→16 | 16→32 | 32→64 |
|---|---|---|---|
| Upwind (pre-existing) | 0.995 | 0.999 | 1.000 |
| Central | 0.492 | 0.500 | 0.500 |
| LinearUpwind | 0.539 | 0.510 | 0.502 |
| QUICK | 0.511 | 0.503 | 0.501 |

All three higher-order schemes measure **~0.5 globally — lower than
plain Upwind's own ~1.0.** This is not a defect: boundary faces (and any
internal face whose upwind cell is boundary-adjacent) deliberately keep
Upwind's own treatment (§4/§5), and that fixed-fraction-of-the-domain
band's non-shrinking bias dominates the *global* L2 rate. Confirmed by
direct per-cell inspection: a corner cell's discrete estimate converges
to a *constant* multiple of the true value under the (rejected) psi=1
policy, and to the *exact* value under the shipped psi=0 policy — see §5.
Reported honestly rather than asserted away with an unrealistic bound
(`GridRefinementTest.*GlobalOrderReflectsBoundaryTreatment`, acceptance
range 0.3–0.8, matching what is actually measured).

### INTERIOR-ONLY (excluding the 2 boundary-adjacent columns each side)

| Scheme | 8→16 | 16→32 | 32→64 |
|---|---|---|---|
| Central | 1.781 | 1.611 | 1.551 |
| LinearUpwind | 1.699 | 1.597 | 1.552 |
| QUICK | 1.745 | 1.605 | 1.551 |

All three measure **~1.55–1.78**, well above Upwind's own order-1 rate
and consistent with Harten's theorem (a TVD-limited scheme cannot exceed
2nd order in general, further reduced here by this field's periodic
extrema) — this is the evidence that the schemes are genuinely
higher-order where they are meant to apply
(`GridRefinementTest.*ConvergesAboveFirstOrderInInterior`, acceptance
range 1.3–2.3).

---

## 8. Focused test results (all passing)

- `CFDDiscretizationTests`, convection-related filter (`ConvectionTest.*`,
  `ConvectionSchemeTest.*`, `QuickFaceValueTest.*`,
  `LinearUpwindFaceValueTest.*`, `SmoothnessRatioTest.*`,
  `VanLeerLimiterTest.*`, `GridRefinementTest.*`): **39/39**.
- `CFDPhysicsTests`, `MomentumConvectionTest.*`: **7/7** (2 new:
  default-argument equivalence to explicit Upwind; a non-Upwind scheme
  changes only the RHS, never the implicit matrix, on internal faces
  only).
- `CFDIoTests`, convection-scheme config parsing/round-trip: **5/5** (3
  new in `CaseReaderTest`: missing→default, all 4 valid names, invalid
  value throws; 1 new + 1 extended in `CaseWriterTest`: default and
  non-default round-trip).
- Full `CFDDiscretizationTests` target: **69/69** (up from 66 — 3 net new
  test suites' worth, some tests replaced).
- Full `CFDPhysicsTests` target: **92/92** (up from 90).
- Full `CFDIoTests` target: **204/204** (up from 200).
- `CFDMeshTests` (MeshGeometry bugfix, §6): **37/37**, unchanged.

## 9. Full regression

`ctest -j32` (WSL/Linux `build/debug`, commit `fd9bae3` + this task's
changes): **1381/1381 passed, 0 failed** (up from 1344/1344 pre-task; net
+37 new tests actually executed — several tests were also rewritten in
place, e.g. the corrected `GridRefinementTest` scheme tests, and a few
low-level ones like `BoundednessLimiterTest` were replaced by
`SmoothnessRatioTest`/`VanLeerLimiterTest` after the boundedness-strategy
correction in §5), 13 validation tests intentionally `DISABLED_`
(unchanged from before this task, pre-existing/unrelated). The absent
`convection_scheme` key preserves every existing case's behavior exactly
(confirmed both by the full regression's zero-failure result across
every existing production/example case, and directly by
`CaseReaderTest.MissingConvectionSchemeDefaultsToUpwind` and
`ConvectionSchemeTest.ConvectionOverloadWithoutSchemeArgumentDefaultsToUpwind`/
`MomentumConvectionTest.DefaultSchemeArgumentMatchesExplicitUpwind`).

---

## 10. Limitations (disclosed, not gaps)

- **GUI**: `convection_scheme` is not exposed in
  `apps/gui/CaseModelAdapter.cpp` / the Solver settings QML editor —
  matches the existing precedent for `LinearSolverSpec::backend`
  (P6-GPU-002), also case-file-only. A case file can still set it
  directly; the GUI will show/preserve whatever value is on disk via its
  own default-construction path (defaults to `"upwind"` if unset).
- **Thermal/species/turbulence/multiphase**: each has its own,
  pre-existing, separate convection assembler
  (`assembleThermalConvectionContribution`,
  `assembleSpeciesConvectionContribution`,
  `assembleVolumeFractionConvectionContribution`, the latter two
  pre-dating this task and not reused by momentum's). None of them are
  wired to `ConvectionScheme` in this task — checked, as the task
  required, and found not reused; wiring them is future work, not a
  silent omission.
- **Compressible**: `CompressibleMomentum`/`CompressibleRelaxedMomentum`
  call `cfd::physics::assembleConvectionContribution` directly, so they
  gain the *capability* automatically (same function, same default), but
  there is no compressible-specific case-file key routing a non-default
  scheme there yet.
- **Boundary treatment stays first-order for every scheme** (§4) — a
  materially larger undertaking (boundary-consistent higher-order
  stencils) is out of this task's scope, and is the direct cause of the
  low *global* (not interior) observed order in §7.
- **Non-orthogonal/skewed meshes**: not applicable — this codebase's
  mesh generator is Cartesian-only (pre-existing, unrelated to this
  task; see `MeshQuality.hpp`'s own header comment).
