# CFDApp — Roadmap

High-level development map. For live status, active gates, and blockers see
`TODO.md`; for detailed verification evidence see `results/`.

---

## Released

**v0.2.0** (commit `1e960c7`, tag `v0.2.0`,
<https://github.com/matlabuser123/CFDAPP/releases/tag/v0.2.0>) — steady
incompressible SIMPLE/transient PISO, thermal + Boussinesq buoyancy,
laminar/k-ε/k-ω/SST turbulence, species/multiphase/compressible physics
fully wired into the production case/CLI/GUI dispatch path, CPU/OpenMP/CUDA
performance, a Qt6/QML case-authoring GUI, and CI/packaging/release
automation. First release was v0.1.5 (build/CLI/core physics only, no GPU
validation or production physics/GUI-authoring integration).

**Known documentation defect (disclosed, not corrected in place):** the
published v0.2.0 release notes' "Known limitations" section says
species/multiphase/compressible are "not yet reachable through
`physics.json`/`ProjectRunner`'s production dispatch" — false against the
source tree actually released (see P10 below). Release evidence is
immutable; the correction applies to this document and future release
notes only.

---

## Completed

**P0–P5 — Core CFDApp.** Numerical foundation (structured-mesh FVM, sparse
linear algebra, SIMPLE), validation (Poiseuille, Ghia cavity), transient
PISO, thermal transport, k-ε/k-ω/SST turbulence, Boussinesq buoyancy
(validated against De Vahl Davis 1983), variable properties, a CLI +
Qt/QML GUI on one shared solver backend, visualization/post-processing,
and release v0.1.5.

**P6 — GPU Performance.** Persistent GPU-resident pipeline, production
GPU CG/BiCGSTAB with CPU fallback, GPU-resident Jacobi preconditioning.
Evidence: `results/performance/preconditioner/`.

**P7 — Performance Validation.** CUDA end-to-end: GPU break-even 80×80,
best speedup 2.01× at 320×320. OpenMP scaling: best 4 threads (1.33×),
regresses at 16/32 threads (WSL2 thread-spawn overhead). Large-grid
stress: stable to 480×480 (CPU) / 640×640 (GPU), memory never the limit.
Evidence: `results/performance/{cuda_end_to_end,openmp_scaling,large_grid_stress}/`.

**P8 — Production Hardening.** Full regression 1289/1289, parallel
`ctest -j8` clean, ASan/UBSan zero diagnostics, clang-format/clang-tidy
clean, native Windows (MSVC/Qt) and WSL2 (CPU+CUDA) builds verified,
manual GUI acceptance 17/17. Evidence: `results/release/p8-hardening/`.

**P9 — v0.2.0 Release.** Tagged/published at `1e960c7`; CI green on that
exact commit (run `34617756507`); GUI acceptance 17/17; packaged CLI/GUI
smoke-tested twice; release assets + checksums verified against GitHub's
own digests (run `34675450224`). Evidence: `results/release/v0.2.0/`.

**P10 — Production Physics Integration** (reconciled — see note below).
Species (`P10-APP-001`), multiphase (`P10-APP-002`), and compressible
(`P10-APP-003`, post-hoc scope) are each configurable via `physics.json`,
dispatched by `ProjectRunner`, exposed in the GUI's `PhysicsEditor.qml`,
and exported to CSV/VTK/JSON, each with a production example case and a
7/7-passing end-to-end regression. `P10-APP-004` consolidated all
cross-physics compatibility rules into one authoritative
`validatePhysicsCompatibility` function, connected to CLI/GUI/JSON
validation alike, 10/10 new tests. Evidence: `results/p10-app-004/summary.md`,
`docs/user_guide/case_format.md`.

*Reconciliation note:* this phase's checklist was unchecked until this
audit even though the work had already shipped in v0.2.0 under an earlier
internal numbering (`P6-PHYS-001/002/003`) — a documentation refactor had
renumbered a stale future-phase template onto already-completed work
without checking the source tree. Re-verified fresh (full regression,
targeted suites) before correcting the record; see `TODO.md`'s history for
the exact commits/counts.

*Disclosed, deliberate scope limits (not gaps):* multiphase mixture
density is not coupled into continuity, only viscosity feeds momentum
(`MultiphaseProperties.hpp`'s own documented scope). Compressible's
two-way energy/pressure-velocity coupling was deferred to `P12-COMP`
rather than committed to under P10 — see below.

**P11 — GUI Case Authoring** ✅. Mesh (`P11-GUI-001`), physics
(`P11-GUI-002`), boundary-condition (`P11-GUI-003`), and solver-settings
(`P11-GUI-004`) editors let a user build a full case without hand-editing
JSON; backend logic for from-scratch case creation is verified
(`CaseEditingTest.FullCaseCreationFromScratchValidatesSavesRunsAndMatchesCli`).
`P11-GUI-005`'s human-executed pass through the actual new-case-creation
workflow (New → mesh → physics → boundaries → solver → Save As → close →
reopen → round-trip → Validate → Run → inspect residuals/results):
13/13 PASS, tester: project owner, 2026-09-13, against a fresh binary at
commit `c25115faefd676ce59ce04d83769c80b9a2d2d3c`. Template-selection
option confirmed present. Evidence:
`results/release/p8-hardening/gui_acceptance.md`'s P11-GUI-005 addendum.

---

## Current — P12

Begin after production integration is stable. Only `P12-COMP` is an active
commitment right now; the rest are planned directions, not commitments.

### P12-COMP — Compressible CFD

- [x] **COMP-001 — EOS-based boundary-density model.** Replaced the
  owner-cell-reuse simplification with a real per-boundary-face EOS
  density evaluation, no new BC types or case-format keys needed. 50/50
  `CFDCompressibleTests`, 7/7 `CFDLowMachRegressionTests` (unchanged),
  full regression 1313/1313. Evidence: `results/p12-comp-001/summary.md`.
- [x] **COMP-002 — coupled compressible pressure-velocity solver.** A
  dedicated `CompressibleSIMPLE` path (new momentum/pressure-correction
  modules, `CompressibleMomentum`/`IdealGasEOS::dDensityDPressure`
  reused) with density as genuinely iterated state (EOS-updated from the
  corrected pressure every outer iteration, not a post-hoc read after
  the loop), gated behind `compressible.coupled` (default `false`,
  today's post-hoc behavior byte-identical when absent). Reduces to
  plain incompressible `SIMPLE` in the low-Mach limit (regression-tested).
  Independent physical validation against Arkilic et al. (1997)'s
  isothermal compressible-channel (lubrication) analytical solution:
  L2/Linf pressure error 1.28%/2.55%, explained by the lubrication
  approximation's own reduced-Reynolds-number error term, not FVM
  discretization error. Root cause of an initial convergence failure
  (unpreconditioned BiCGSTAB breakdown on the — symmetric —
  pressure-correction matrix, right at the tail of convergence) found
  and fixed via a pure case-configuration choice (CG instead of
  BiCGSTAB for that case's pressure solve), not a solver-code change.
  Evidence: `results/p12-comp-002/summary.md`.
- [ ] Advanced compressible-energy formulation (two-way viscous-dissipation/
  pressure-work coupling) — explicitly deferred past COMP-002.
- [ ] Higher-Mach capability — explicitly deferred past COMP-002.

### Other planned P12 directions

Not started, not scoped in detail yet:

- **P12-NUM** — higher-order convection schemes, additional
  preconditioners, multigrid, a fully coupled pressure-based solver.
- **P12-TURB** — advanced turbulence validation, additional production
  turbulence capabilities where justified.
- **P12-SPECIES** — additional species models, reaction/source-term
  framework.
- **P12-MULTI** — advanced interface methods, surface tension, interface
  reconstruction.
- **P12-MESH** — moving/deforming meshes, 3D foundation.

Do not silently start any of these, or compressible energy/higher-Mach
work, without an explicit scope decision recorded in `TODO.md`.

---

## Future

**P13 — Production Maturity** (long-term hardening, not yet scoped in
detail): crash reporting and structured diagnostic logging; long-duration
and large-case stress tests; result-comparison tooling and a benchmark
dashboard; backward-compatible case-schema migration; a plugin/model
extension architecture; cross-platform (Linux) packaging and installer
testing; a formal release-candidate qualification process.

P13's reliability/compatibility/distribution items may proceed alongside
later P10/P11 follow-ups once those are far enough along, without waiting
for all of P12 — but only with explicit authorization in `TODO.md`, never
silently.

---

## Principles

1. Numerical correctness before optimization; validation before
   performance claims.
2. New physics requires physical validation, not only unit tests, and
   requires production-path integration (case format → dispatch → export),
   not just an equation-level module.
3. The CPU reference implementation is authoritative; GPU/OpenMP paths
   require verified numerical equivalence, not just a successful build.
4. Failed or excluded cases stay documented, not hidden.
5. A roadmap item is complete only after implementation, testing,
   validation, integration, documentation, and production usability — see
   `CLAUDE.md` for what evidence that requires.
