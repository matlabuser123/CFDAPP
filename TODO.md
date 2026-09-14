# CFDApp — TODO

**Released:** v0.2.0 (`1e960c7`, tag `v0.2.0`)

**Current development state:** P12-COMP-001/002 and P12-NUM-001–007 complete.

**Evidence rule:** `[x]` means implemented **and verified with real evidence**.
Never mark an item complete from implementation alone.

See:

* `CLAUDE.md` — workflow/evidence rules
* `ROADMAP.md` — high-level development history and direction
* `results/` — detailed verification evidence

---

# Current

## P12-NUM Closeout — `[x]` COMPLETE (2026-09-15)

* [x] Review the complete P12-NUM working-tree diff.
* [x] Remove/revert generated files, runtime-only changes and accidental artifacts.
* [x] Confirm `results/p12-num-001/` through `results/p12-num-007/` contain the intended evidence.
* [x] Confirm `TODO.md`, `ROADMAP.md` and validation documentation agree with the final implementation.
* [x] Run final focused smoke checks if required by `CLAUDE.md`.
* [x] Commit P12-NUM-001 through P12-NUM-007.
* [x] Push the commit.
* [x] Confirm CI passes on the exact pushed commit.
* [x] Record the final commit SHA and CI run in the closeout evidence.

Evidence: `results/p12-num-closeout/summary.md` (§1 audit, §2 cleanup, §3
evidence, §4 docs, §5 verification: Release 1614/1614 + GUI 40/40, ASan 0
diagnostics; §6 pre-existing CI failures and their fixes; §7 commits/CI).

* P12-NUM commit: `105383d1c026f2cad1b19753250e4a931065cca0`; CI-fix commit:
  `44b996a39f3884bd64937c732a4c8c6e61723dd6` (pushed, = `origin/main`).
* CI run `34839669398` on exact SHA `44b996a…`: success, all 7 jobs green
  (format, python, clang-tidy, gcc-release, clang-debug, gcc-debug,
  sanitizers; each test job 1614/1614, 25 disabled, 0 sanitizer diagnostics).

**The closeout is complete. No next phase is authorized — a new explicit scope
decision is required before any further P12 work (see below).**

---

# Next Scope Decision

## Recommended: P12-MESH — Production Mesh Capability

**Status:** proposed, **not authorized yet**.

Goal: remove the current uniform-Cartesian production-mesh limitation and make the numerical methods developed in P12-NUM usable on realistic production geometries.

### P12-MESH-001 — Production Non-Orthogonal Structured Meshes

* [ ] Define production mesh representation requirements.
* [ ] Support non-orthogonal quadrilateral meshes through the production case path.
* [ ] Preserve existing Cartesian case behavior.
* [ ] Connect production meshes to existing:

  * [ ] Green–Gauss gradients
  * [ ] least-squares gradients
  * [ ] skewness correction
  * [ ] non-orthogonal diffusion
  * [ ] geometric pressure correction
  * [ ] SIMPLE
  * [ ] CompressibleSIMPLE
  * [ ] thermal transport
  * [ ] species transport
  * [ ] turbulence transport
* [ ] Add mesh validity checks.
* [ ] Add deterministic mesh regression tests.
* [ ] Add end-to-end distorted production case.
* [ ] Verify conservation and convergence.
* [ ] Verify existing Cartesian results are not regressed.
* [ ] Add evidence under `results/p12-mesh-001/`.

**Gate:** a non-orthogonal mesh loaded through the normal production case path must run a physically validated CFD case successfully.

---

### P12-MESH-002 — Stretched / Graded Meshes

* [ ] Add x-direction grading.
* [ ] Add y-direction grading.
* [ ] Add boundary clustering.
* [ ] Support geometric progression controls.
* [ ] Validate cell sizes and grading ratios.
* [ ] Prevent zero/negative/degenerate cells.
* [ ] Expose grading in the case format.
* [ ] Preserve uniform-grid defaults.
* [ ] Add CLI/case round-trip tests.
* [ ] Add GUI support if required for production usability.
* [ ] Validate wall-resolved Poiseuille/channel case.
* [ ] Compare accuracy/cost against uniform meshes.
* [ ] Add grid-convergence evidence.
* [ ] Add evidence under `results/p12-mesh-002/`.

**Gate:** demonstrate quantitatively that mesh clustering improves resolution of a wall-gradient problem without violating conservation.

---

### P12-MESH-003 — General 2D Geometry

* [ ] Define the minimum supported general-geometry architecture.
* [ ] Support multiple connected blocks and/or body-fitted structured regions.
* [ ] Support non-rectangular fluid domains.
* [ ] Support internal solid/inactive regions if compatible with the architecture.
* [ ] Generate correct face connectivity.
* [ ] Generate boundary patches from geometry.
* [ ] Validate face orientation and ownership.
* [ ] Detect invalid/overlapping cells.
* [ ] Detect disconnected fluid regions.
* [ ] Preserve conservation across block interfaces.
* [ ] Export geometry correctly to VTK.
* [ ] Add representative production cases.
* [ ] Add evidence under `results/p12-mesh-003/`.

**Do not silently turn this into a full unstructured-meshing project.**

**Gate:** solve and quantitatively validate at least one genuinely non-rectangular 2D production case.

---

### P12-MESH-004 — Mesh Quality & Validation

* [ ] Consolidate production mesh-quality diagnostics.
* [ ] Report:

  * [ ] minimum cell volume/area
  * [ ] aspect ratio
  * [ ] non-orthogonality
  * [ ] skewness
  * [ ] grading/expansion ratio
  * [ ] degenerate cells
* [ ] Define warning thresholds.
* [ ] Define fatal-invalid thresholds.
* [ ] Surface diagnostics through CLI.
* [ ] Surface diagnostics through GUI where appropriate.
* [ ] Export diagnostics to results JSON.
* [ ] Verify numerical behavior across controlled mesh-quality degradation.
* [ ] Run MMS on production non-orthogonal meshes.
* [ ] Run grid-convergence studies.
* [ ] Add evidence under `results/p12-mesh-004/`.

**Gate:** mesh-quality degradation must be measurable and its numerical effect quantitatively demonstrated.

---

### P12-MESH-005 — 3D Foundation

**Do not start until P12-MESH-001–004 are complete.**

* [ ] Generalize mesh primitives from 2D to 3D.
* [ ] Add 3D cell volumes.
* [ ] Add 3D face-area vectors.
* [ ] Add 3D cell/face centroids.
* [ ] Add six-face Cartesian hexahedral connectivity.
* [ ] Generalize scalar fields to 3D meshes.
* [ ] Generalize vector fields to three components.
* [ ] Generalize boundary patches.
* [ ] Generalize interpolation.
* [ ] Generalize gradients.
* [ ] Generalize diffusion.
* [ ] Generalize convection.
* [ ] Generalize sparse-system assembly.
* [ ] Add 3D VTK export.
* [ ] Add unit/operator MMS tests.
* [ ] Add evidence under `results/p12-mesh-005/`.

**Gate:** 3D operators reproduce analytical fields at their expected spatial order.

---

### P12-MESH-006 — 3D Incompressible Solver

* [ ] Add w-momentum equation.
* [ ] Extend SIMPLE pressure–velocity coupling to 3D.
* [ ] Extend face-flux correction to 3D.
* [ ] Extend continuity diagnostics to 3D.
* [ ] Extend non-orthogonal correction to 3D.
* [ ] Extend solver residual reporting.
* [ ] Add 3D manufactured solution.
* [ ] Add 3D Poiseuille/channel validation.
* [ ] Add 3D lid-driven-cavity or equivalent benchmark.
* [ ] Verify global mass conservation.
* [ ] Add CPU performance baseline.
* [ ] Add evidence under `results/p12-mesh-006/`.

**Gate:** production 3D SIMPLE must converge from a non-exact initial state and agree quantitatively with an independent analytical/benchmark solution.

---

### P12-MESH-007 — Moving / Deforming Mesh Foundation

**Do not start until the static 3D foundation is stable.**

* [ ] Define mesh-motion architecture.
* [ ] Preserve topology for deformation-only motion.
* [ ] Update cell volumes and face geometry.
* [ ] Implement mesh velocity.
* [ ] Implement ALE transport foundation.
* [ ] Enforce the geometric conservation law.
* [ ] Add moving-wall support.
* [ ] Add prescribed mesh-motion test.
* [ ] Add conservation regression.
* [ ] Add manufactured/analytical validation where possible.
* [ ] Add evidence under `results/p12-mesh-007/`.

**Gate:** uniform flow must remain uniform under prescribed mesh motion to numerical tolerance.

---

# Future P12 Scope

These remain **unauthorized** until an explicit scope decision is recorded.

## P12-COMP

Completed:

* [x] COMP-001 — EOS-based boundary-density model.
* [x] COMP-002 — coupled compressible pressure–velocity solver.

Deferred:

* [ ] Advanced compressible-energy formulation.
* [ ] Pressure-work coupling.
* [ ] Viscous-dissipation coupling.
* [ ] Higher-Mach capability.
* [ ] Compressible energy validation.
* [ ] Higher-Mach benchmark validation.

---

## P12-TURB

Potential future scope:

* [ ] Establish stronger turbulence-validation baseline.
* [ ] Fully developed turbulent-channel validation against DNS profiles.
* [ ] Improve wall-treatment architecture.
* [ ] Add wall functions where justified.
* [ ] Validate k-ε.
* [ ] Validate k-ω.
* [ ] Validate SST.
* [ ] Add separated-flow turbulence validation.
* [ ] Quantify mesh/y+ sensitivity.
* [ ] Add turbulence grid-convergence studies.

Do not add new turbulence models merely to increase model count.

---

## P12-SPECIES

Potential future scope:

* [ ] General source-term framework.
* [ ] Multiple transported species.
* [ ] Species-dependent diffusivity.
* [ ] Reaction source terms.
* [ ] Coupled reactions.
* [ ] Conservation enforcement.
* [ ] Thermal/species coupling.
* [ ] Production validation cases.
* [ ] Manufactured-solution verification.

---

## P12-MULTI

Potential future scope:

* [ ] Couple mixture density consistently where physically required.
* [ ] Interface reconstruction.
* [ ] Interface compression.
* [ ] Surface tension.
* [ ] Curvature calculation.
* [ ] Contact-angle treatment.
* [ ] Bounded volume-fraction transport.
* [ ] Static-droplet validation.
* [ ] Advection validation.
* [ ] Capillary-wave validation.

Do not claim full multiphase CFD from the existing foundation alone.

---

# P13 — Production Maturity

**Status:** future; not authorized.

Potential scope:

* [ ] Structured diagnostic logging.
* [ ] Crash reporting.
* [ ] Long-duration stress testing.
* [ ] Large-case stress testing.
* [ ] Benchmark dashboard.
* [ ] Automated result comparison.
* [ ] Case-schema migration/versioning.
* [ ] Backward-compatibility testing.
* [ ] Plugin/model extension architecture.
* [ ] Linux packaging.
* [ ] Cross-platform installer testing.
* [ ] Formal release-candidate qualification.
* [ ] Performance-regression tracking.

---

# Blocked / Manual

* [ ] Native Windows + CUDA validation.

  * WSL2/Linux + CUDA is verified.
  * Requires the appropriate native Windows/CUDA build environment and hardware path.

---

# Completed

## Releases

* [x] v0.1.5
* [x] v0.2.0

## Major Phases

* [x] P0–P5 — Core CFDApp
* [x] P6 — GPU Performance
* [x] P7 — Performance Validation
* [x] P8 — Production Hardening
* [x] P9 — v0.2.0 Release
* [x] P10 — Production Physics Integration
* [x] P11 — GUI Case Authoring
* [x] P12-COMP-001
* [x] P12-COMP-002

## P12-NUM — Numerical Methods & Robustness

* [x] NUM-001 — Higher-order convection
* [x] NUM-002 — Gradient reconstruction
* [x] NUM-003 — Non-orthogonal/skewness correction
* [x] NUM-004 — Solver robustness
* [x] NUM-005 — Grid convergence
* [x] NUM-006 — Manufactured solutions
* [x] NUM-007 — Production validation

Final NUM-007 backward-facing-step verification (Gartling 1990 configuration,
Re = 800, expansion ratio 2; lengths in step heights h = H/2; cells/H = cells
per channel height; the 50 cells/H level is a pure refinement run through the
unchanged implementation — see `results/p12-num-007/summary.md` §8):

```text
20 cells/H : x_r/h = 10.085 ; upper bubble (x_rs - x_s)/h = 12.143
30 cells/H : x_r/h = 11.405 ; upper bubble (x_rs - x_s)/h = 11.863
40 cells/H : x_r/h = 11.780 ; upper bubble (x_rs - x_s)/h = 11.626
50 cells/H : x_r/h = 11.932 ; upper bubble (x_rs - x_s)/h = 11.506
```

Published 2D benchmark ranges (10 studies incl. Gartling, compiled in
arXiv:2507.16509 Table 2):

```text
lower-wall reattachment x_r/h:  11.48–12.20  -> PASS
upper-wall bubble (x_rs-x_s)/h: 10.60–11.52  -> PASS
```

P12-NUM final regression:

```text
1654 / 1654 passed
0 failed
(1679 listed, 25 disabled/explicit-only)
```

Evidence:

```text
results/p12-num-001/
results/p12-num-002/
results/p12-num-003/
results/p12-num-004/
results/p12-num-005/
results/p12-num-006/
results/p12-num-007/
```

---

# Working Rules

1. Work top-to-bottom.
2. Stop at failed acceptance gates.
3. `[x]` requires implementation **and real verification evidence**.
4. Never weaken a quantitative gate merely because an implementation fails it.
5. Preserve failed experiments and explain their cause.
6. Numerical correctness comes before optimization.
7. New physics requires independent physical validation.
8. New numerical methods require quantitative convergence verification.
9. Production capability means case format → parser → solver dispatch → results/export and, where required, GUI integration.
10. Preserve backwards compatibility unless an explicit breaking change is authorized.
11. Keep generated/runtime-only noise out of commits.
12. Record evidence under `results/<phase>/`.
13. Run focused tests before the full regression.
14. Run the full regression before closing a phase.
15. Record the exact test counts; never estimate them.
16. Do not commit or push unless explicitly authorized.
17. Do not begin another major phase without an explicit scope decision.
