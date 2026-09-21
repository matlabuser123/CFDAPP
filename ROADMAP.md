# CFDApp — Roadmap

> **Where is the project going?** This file covers capability, direction and dependency order.
> Current tasks: [TODO.md](TODO.md) · Rules: [CLAUDE.md](CLAUDE.md) · Evidence: [results/](results/)

**Status legend** (shared with `TODO.md`):

| Status | Meaning |
| --- | --- |
| **COMPLETE** | Implemented and verified with evidence. |
| **ACTIVE** | Authorized and in progress. |
| **BLOCKED** | Authorized, but stopped at a failed gate or waiting on a dependency. |
| **PLANNED** | The next step in an agreed sequence, not yet authorized. |
| **NOT AUTHORIZED** | A candidate direction only. |

A phase listed here is **not** authorized. Work starts only after an explicit scope decision, which
is recorded in `TODO.md`.

---

## Vision

CFDApp aims to be a **trustworthy general-purpose finite-volume CFD platform**. Every capability is:

* numerically verified (analytical solutions, MMS, grid convergence, conservation);
* physically validated (literature benchmarks);
* reachable through one production path, from case file to CLI/GUI to export.

It grows from a validated 2D structured-mesh solver towards production meshes, 3D, moving
geometry and richer physics — in that dependency order, with evidence at every step.

---

## Current Capability

As of 2026-09-17. **Released** means tagged `v0.2.0`. **Main** means committed and pushed but not
yet tagged. **Uncommitted** means verified in the working tree but not yet committed.

| Area | Capability | Level | Main limits |
| --- | --- | --- | --- |
| Incompressible 2D | SIMPLE (steady), PISO (transient), CG/BiCGSTAB/GMRES | Released | — |
| Thermal | Energy equation, Boussinesq buoyancy, conjugate heat-transfer foundation | Released | — |
| Turbulence | k-ε, k-ω and SST RANS, validated on channel flow | Released | No DNS-profile or separated-flow validation |
| Species / multiphase | Passive species; two-phase volume fraction with mixture viscosity | Released | Mixture density not coupled into continuity (deliberate) |
| Compressible | Post-hoc EOS (default); coupled `CompressibleSIMPLE` (opt-in) | Main | No two-way energy coupling; higher Mach deferred; CPU only |
| Numerics | Four convection schemes, Green–Gauss / least-squares gradients, non-orthogonal correction, robustness, GCI, MMS | Main | Scalar transport upwind-only; no multigrid |
| Production meshes | Non-orthogonal, graded and multi-block 2D structured meshes; mesh-quality report | Uncommitted | Structured quads only; no unstructured meshes |
| 3D | Cartesian hexahedral mesh and operators; steady laminar 3D SIMPLE with Rhie–Chow | Uncommitted | Uniform boxes; single-threaded; no 3D physics beyond laminar flow; no GUI 3D view |
| Moving mesh | ALE/GCL library API: 2D `AlePISO`; 3D geometry, GCL and ALE operators | Uncommitted, complete | C++ API only (not in case format, CLI or GUI); no 3D ALE flow solver; no moving-mesh MMS |
| Performance | GPU-resident CG/BiCGSTAB with CPU fallback; OpenMP SpMV | Released | GPU break-even ≈ 80×80; no GPU 3D |
| CUDA SIMPLE | The whole SIMPLE outer iteration on the device — operators, both linear solves, corrections — with nothing uploaded in steady state | Uncommitted, complete | Reachable only through the C++ `SIMPLESettings` API: the case format has no `enableGpuDiscretization` key. Declined for non-laminar turbulence, CG, CPU solver backends, solver fallback and the residency mirror. WSL2 CUDA only |
| Application | CLI and Qt6/QML GUI on one `ProjectRunner`; case authoring; VTK/CSV/JSON export | Released | Native Windows CUDA not validated |

---

## Development Principles

1. **Numerical correctness before optimization.** Validation comes before any performance claim.
2. **New physics needs independent physical validation.** New numerical methods need quantitative
   convergence verification.
3. **A capability is production capability only on the production path:** case format → parser →
   builder → dispatch → export → CLI/GUI. A library API alone does not count.
4. **The CPU implementation is the reference.** GPU and OpenMP paths must show verified equivalence.
5. **Failed and excluded results stay documented.** Amendments are recorded, never rewritten.
6. **Dependencies first.** A capability is built on a verified foundation, not ahead of it.

The binding working rules are in [CLAUDE.md](CLAUDE.md).

---

## Completed

### P0–P5 — Core CFD — COMPLETE (v0.1.5)

Delivered:

* structured-mesh FVM, sparse linear algebra, SIMPLE and PISO;
* thermal transport, k-ε/k-ω/SST turbulence and Boussinesq buoyancy;
* variable properties;
* a CLI and a Qt/QML GUI on one solver backend;
* visualization and post-processing.

Validated against Poiseuille flow, the Ghia cavity and de Vahl Davis (1983).

### P6–P9 — Performance, Hardening, Release — COMPLETE (v0.2.0)

* **P6 GPU performance.** A persistent GPU-resident pipeline, GPU CG/BiCGSTAB with CPU fallback,
  and GPU Jacobi preconditioning.
* **P7 performance validation.** End-to-end CUDA and OpenMP scaling, plus large-grid stress tests.
  Evidence: `results/performance/`.
* **P8 production hardening.** Full regression, sanitizers, clang-format/clang-tidy, native Windows
  (MSVC/Qt) and WSL2 builds, and a manual GUI acceptance run. Evidence:
  `results/release/p8-hardening/`.
* **P9 release.** `v0.2.0` tagged at `1e960c7`, with CI green and verified assets. Evidence:
  `results/release/v0.2.0/`.

*Disclosed documentation defect.* The published v0.2.0 release notes say that species, multiphase
and compressible are not reachable through production dispatch. That is false for the released
source (see P10). Release evidence is immutable, so the correction applies here and in future notes
only.

### P10 — Production Physics Integration — COMPLETE

Delivered:

* species, multiphase and compressible physics, configured through `physics.json`, dispatched by
  `ProjectRunner`, editable in the GUI and exported;
* one authoritative cross-physics compatibility validator (`validatePhysicsCompatibility`).

Evidence: `results/p10-app-004/`. The work shipped in v0.2.0 under the earlier numbering
`P6-PHYS-001/002/003`, and its record was reconciled afterwards.

### P11 — GUI Case Authoring — COMPLETE

Delivered:

* mesh, physics, boundary-condition and solver-settings editors;
* new-case-from-scratch, verified by a human-executed acceptance pass.

Evidence: `results/release/p8-hardening/gui_acceptance.md` (P11-GUI-005 addendum).

### P12 — Numerical & Mesh Foundation

#### P12-COMP — Compressible — COMPLETE (COMP-001, COMP-002)

Delivered:

* per-boundary-face EOS density;
* a dedicated coupled `CompressibleSIMPLE`, with density as iterated state:
  * opt-in via `compressible.coupled` (default behaviour unchanged);
  * reduces to SIMPLE in the low-Mach limit;
  * validated against the Arkilic et al. (1997) isothermal-channel solution.

Limits:

* no turbulence or buoyancy injection point;
* CPU only;
* energy coupling and higher Mach are deferred.

Evidence: `results/p12-comp-001/`, `results/p12-comp-002/`.

#### P12-NUM — Numerical Methods & Robustness — COMPLETE (committed `105383d`)

Delivered:

* upwind, central, linear-upwind and QUICK convection (bounded deferred correction);
* Green–Gauss and weighted least-squares gradients;
* non-orthogonal and skewness correction across all transport equations;
* solver robustness: normalized residuals, stagnation/divergence detection, adaptive relaxation,
  GMRES and linear-solver fallback;
* a grid-convergence framework (observed order, Richardson extrapolation, GCI);
* system-level MMS;
* production validation: the Ghia cavity, Poiseuille flow, a turbulent channel at Re_τ = 180, and
  the Gartling backward-facing step at Re = 800 (within published ranges).

Evidence: `results/p12-num-001/` … `results/p12-num-007/`, `results/p12-num-closeout/`.

#### P12-MESH-001–006 — Production Meshes and 3D Foundation — COMPLETE (verified, uncommitted)

| Phase | Capability gained |
| --- | --- |
| MESH-001 | Non-orthogonal structured quad meshes through the production path (`"structured_quad"`), with a validated distorted-Poiseuille case |
| MESH-002 | Geometric grading and wall clustering (`"grading"`); clustering shown to cut wall-shear error at equal cell count |
| MESH-003 | Conformal 2D multi-block geometry (`"multiblock"`, named patches, holes); curved channel validated at second order; interfaces conservative |
| MESH-004 | One authoritative mesh-quality report (CLI, GUI, JSON); invalid-mesh rejection; quantified degradation response; scale-invariant BiCGSTAB breakdown test |
| MESH-005 | A dimension-independent `Vector3`/`Mesh` stack, Cartesian hexahedra, and 3D operators verified at the expected order (C++ API only) |
| MESH-006 | 3D steady laminar SIMPLE: w-momentum, 3D Rhie–Chow, the 3D case format, CLI/GUI, conservation diagnostics; validated on a square duct, the Re = 1000 lid-driven cube and 3D MMS |

Limits:

* 3D covers uniform Cartesian boxes and steady laminar SIMPLE only;
* PISO, turbulence, thermal, species, multiphase and compressible refuse 3D;
* 3D is single-threaded, with no GPU 3D.

MESH-006 passed under gate Amendment A3; its original failure stays on record. Evidence:
`results/p12-mesh-001/` … `results/p12-mesh-006/`.

---

## Current Numerical Foundation

Moving meshes exposed a chain of pre-existing boundary-treatment defects. They were resolved in
dependency order, and all three phases closed on 2026-09-17:

```text
MESH-007 G6.3 fails (Galilean invariance)
  └─ cause: Green–Gauss boundary gradient switches branch on exact alignment
       └─ GRAD-002 removes the branch (continuous, second order)
            └─ exposes a first-order Dirichlet wall flux (half-cell one-sided difference)
                 └─ DIFF-002 makes the wall flux second order
```

Resolution order: **DIFF-002 → GRAD-002 → MESH-007**.

### P12-DIFF-002 — Second-Order Dirichlet Boundary Diffusion — COMPLETE (2026-09-17, uncommitted)

Adds a one-sided quadratic wall reconstruction to momentum, thermal, species and k-ε, with a
topology-based fallback.

* Wall-flux order rises from 1 to 2, and the reconstruction is exact for quadratic fields.
* Production dp/dx error improves about 3×.
* Outer-iteration counts stay within the frozen 1.25× convergence guard.
* Two production defects found on the way were fixed: the conjugate thermal-interface wall flux,
  and the divergence-detector floor.
* Legacy validation tests that encoded the superseded scheme were migrated one at a time, each
  against an independently derived reference.
* The W8 grid-convergence validation passed under amendment W8B. It needed the two validation cases
  to select Rhie–Chow (GRAD-002-DRIFT-001).
* Backward compatibility passed under W9A.
* The W10 full regression passes: Release 1923/1923, Debug + GUI 1975/1975, and ASan + UBSan
  1923/1923 with 0 diagnostics.

Every original failure stays on record. Evidence: `results/p12-diff-002/` (see `summary.md`).
Superseded predecessor: P12-DIFF-001 (`results/p12-diff-001/`).

### P12-GRAD-002 — Continuous Green–Gauss Boundary Treatment — COMPLETE (2026-09-17, uncommitted)

Replaces the exact-alignment branch with a continuous, boundary-consistent face value.

* It is translation-invariant and continuous in the geometry.
* It keeps Cartesian second-order accuracy.
* On distorted meshes, the boundary-ring gradient improves from first to second order.
* On the closed DIFF-002 tree, the production-accuracy obstacle is gone: GRAD-002 changes the two
  W8 cases by ≤ 1.3 % (mostly improvements).
* It closed under Amendments A1–A3. The fresh gate passes, including the A1-form criteria rerun on
  the final library.
* The A3 full regression passes: Release 1932/1932, Debug + GUI 1984/1984, and ASan + UBSan
  1932/1932 with 0 diagnostics.

Every original failure stays on record. Evidence: `results/p12-grad-002/` (see `summary.md`).
Superseded predecessor: P12-GRAD-001 (`results/p12-grad-001/`).

### P12-MESH-007 — Moving / Deforming Mesh Foundation — COMPLETE (2026-09-17, uncommitted; library-level C++ API)

Delivers an ALE transport foundation:

* mesh velocity;
* exact swept-volume geometric conservation;
* moving walls;
* a 2D `AlePISO`, with 3D covered at the geometry, GCL and operator level only.

Results:

* Uniform flow stays uniform to round-off under mesh motion.
* The Galilean-invariance gate G6.3/G7.3, rerun unchanged after GRAD-002 and DIFF-002, passes at
  1.9e-13 against 1e-8. The original 2.49e-2 failure stays on record.
* G2.3, G9 (under Amendment A3: the reference is the current tree without MESH-007) and G10 pass.
* A performance baseline is recorded.

**Scope, by user decision: library/API only, not a production capability** (CLAUDE.md §9). The
case format, CLI and GUI are unchanged and stay steady-SIMPLE only.

Evidence: `results/p12-mesh-007/`.

---

## Near-Term — PLANNED

The chain above is closed. Each item below still needs explicit authorization.

1. **Commit and push the verified P12 lineage** (MESH-001 … MESH-007, GRAD-002, DIFF-002,
   ASAN-001). The MESH-004 ASan test defect is fixed (P12-ASAN-001; 0 sanitizer diagnostics in
   DIFF-002 W10 and GRAD-002 A3). A green CI run on the exact SHA is still required.
2. **Numerical debt surfaced by P12.** Each item would be its own scoped phase:
   * warped 3D face integration;
   * CG scale-invariant breakdown;
   * irregular-mesh convergence;
   * the 2D default face flux (Linear) has an undamped odd-even pressure mode on open domains
     (GRAD-002-DRIFT-001). The fix is a Rhie–Chow default, which changes every 2D result;
   * the Green–Gauss gradient's fixed four-sweep loop does not converge on 3D meshes with
     uniformly tilted boundaries (GRAD-002 A2; classified as debt in A3 §5);
   * the CUDA toolchain (WSL nvcc 11.5, architecture 52) predates the Ada GPU (compute capability
     8.9). It must be upgraded before producing GPU performance evidence;
   * the explicit `diffusion()` two-cell defect;
   * large-coordinate structured-quad geometry.
   The full list is under Known Technical Debt in `TODO.md`.
3. **A tagged release** containing P12, with release notes that disclose its limits.

---

## Medium-Term — NOT AUTHORIZED

### P12-TURB — Turbulence validation

* DNS-profile validation of a fully developed turbulent channel
* Wall-treatment architecture, with wall functions where justified
* Separated-flow validation
* y⁺ / mesh sensitivity studies
* Turbulence grid-convergence studies

Do not add turbulence models merely to increase the model count.

### P12-SPECIES — Reacting and multi-species transport

* A general source-term framework
* Species-dependent diffusivity
* Reactions and coupled reactions
* Thermal/species coupling
* Conservation enforcement
* MMS and production validation

### P12-MULTI — Interface methods

* Consistent mixture-density coupling, where physically required
* Interface reconstruction and compression
* Bounded volume-fraction transport
* Surface tension, curvature and contact angle
* Static-droplet, advection and capillary-wave validation

Do not claim full multiphase CFD from the existing foundation alone.

### P12-COMP — Compressible follow-ups

* An advanced compressible-energy formulation
* Pressure-work and viscous-dissipation coupling
* Higher-Mach capability and benchmark validation

### Numerical infrastructure

* Rhie–Chow as the 2D default (currently opt-in for 2D, the default in 3D)
* Multigrid and additional preconditioners
* A fully coupled pressure-based solver
* Higher-order scalar transport (currently upwind-only)

---

## Long-Term — NOT AUTHORIZED

What remains before CFDApp is a serious general CFD platform:

* **3D physics parity:** transient PISO, thermal, turbulence, species and compressible flow in 3D.
* **General 3D geometry:** non-Cartesian and multi-block 3D meshes, and a 3D production mesh path.
* **Moving-geometry production path:** ALE through the case format, CLI and GUI.
* **Scalable 3D performance:** multithreaded and GPU 3D solves; removing the O(boundary faces)
  boundary lookup.
* **3D visualization in the GUI** (ParaView is used today).

---

## Production Maturity — P13 — NOT AUTHORIZED

Production maturity means CFDApp can be relied on across versions, platforms and long runs:

* structured diagnostic logging and crash reporting;
* long-duration and large-case stress testing;
* automated result comparison, a benchmark dashboard and performance-regression tracking;
* case-schema versioning, migration and backward-compatibility testing;
* a plugin/model extension architecture;
* Linux packaging, cross-platform installer testing, and formal release-candidate qualification;
* native Windows + CUDA validation (manual; only WSL2 CUDA is verified today).

P13 reliability and distribution items may run alongside later P12 work, but only with explicit
authorization.

---

## Explicit Non-Goals / Deferred Scope

* **No silent turn to general unstructured meshing.** MESH-003 is conformal multi-block structured
  meshing by design.
* **No turbulence model proliferation** without a validation need.
* **No full-multiphase claim** from the volume-fraction foundation. Mixture density stays
  uncoupled from continuity by design.
* **No moving-mesh case/CLI/GUI integration** in MESH-007 (library API only, by user decision).
* **No higher-Mach or compressible-energy work** until explicitly scoped.
* **No performance work that changes numerics** without equivalence evidence.
