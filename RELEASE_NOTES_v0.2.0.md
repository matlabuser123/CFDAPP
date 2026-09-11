# CFDApp v0.2.0 — Release Notes

GPU performance and production-hardening release. Builds on v0.1.5's core
solver/case-manager/GUI foundation with a persistent GPU-resident SIMPLE
pipeline, measured CPU/GPU/OpenMP performance validation, a full production
hardening pass (sanitizers, static analysis, native Windows + Linux/CUDA
verification, human-executed GUI acceptance), and this release's own
qualification evidence.

## Major changes since v0.1.5

- **Persistent GPU pipeline**: fields and sparse matrices stay resident on
  device across outer SIMPLE iterations, with minimized host/device
  transfers (previously the GPU path covered a single SpMV kernel and
  measured slower end-to-end than CPU — see v0.1.5's "CUDA / GPU status").
- **Production GPU linear solvers**: GPU CG and BiCGSTAB are selectable in
  the production SIMPLE path via `LinearSolverSettings`, with a robust CPU
  fallback and verified CPU/GPU numerical equivalence
  (`SIMPLEGpuSolverTest.GpuBackendReproducesCpuCavitySolutionWithinTolerance`).
- **GPU-resident Jacobi preconditioning**: diag(A)⁻¹ built and applied
  entirely on device.
- **Measured performance validation** (real, end-to-end, never kernel-only):
  - CUDA end-to-end: GPU break-even at **80×80** (6,400 cells); best measured
    speedup **2.01× at 320×320**.
  - OpenMP scaling: best at **4 threads (1.33× speedup)**; regresses
    severely at 16/32 threads (measured WSL2 thread-spawn-overhead effect,
    reported honestly, not hidden).
  - Large-grid stress: largest stable CPU grid **480×480**, GPU **640×640**;
    limiting factor is solver iteration budget, not memory.
- **Production hardening**: full regression suite, parallel `ctest`,
  AddressSanitizer + UndefinedBehaviorSanitizer (zero diagnostics),
  `clang-format`/`clang-tidy` (zero violations), a genuine native Windows
  build and a genuine WSL2/Linux build (CPU-only and CUDA-enabled on real
  GPU hardware) both independently verified.
- **Manual GUI acceptance**: 17/17 PASS, human-executed against the built
  `cfdapp_gui.exe`. One real bug found and fixed along the way: the Case
  page's Directory field rejected a path pasted via Windows Explorer's
  "Copy as path" (wrapped in literal double quotes) — fixed
  (`sanitizeCaseDirectory()` in `apps/gui/SimulationController.cpp`), with a
  regression test.
- **TODO.md / ROADMAP.md restructured**: `TODO.md` is now the single live
  execution checklist (with an explicit evidence-discipline rules section);
  `ROADMAP.md` is the long-term direction document and no longer duplicates
  live status or collides on phase numbers with `TODO.md`.

## Numerical / validation status

Full regression suite, both counted the same way v0.1.5 was:

```text
1290 / 1290 active tests passing
(13 pre-existing, deliberately DISABLED_ slow grid-refinement cases
 excluded — documented, unrelated to this release)
```

Verified on native Windows (MSVC 19.51, Visual Studio 2026, Qt 6.9.3) and
WSL2/Ubuntu 22.04 (CPU-only, and with `-DCFDAPP_ENABLE_CUDA=ON` on a real
NVIDIA GPU). AddressSanitizer + UndefinedBehaviorSanitizer: zero diagnostics
(grepped directly from the log, not inferred from exit code alone). Verified
green end-to-end in GitHub Actions CI across every mandatory job (`format`,
`sanitizers`, `build-test` × 3 compiler/build-type combinations,
`clang-tidy`, `python`) — see `results/release/v0.2.0/summary.md` for the
exact run evidence, including one real CI-only scheduling bug found and
fixed (the `sanitizers` job's parallelism was tuned against a 32-core local
workstation and oversubscribed GitHub's 4-vCPU runners; fixed to size itself
to the actual runner).

## CLI / GUI status

Unchanged in scope from v0.1.5 (see those release notes) — the GUI still
cannot author cases from scratch (mesh/physics/BC/solver editors); that
remains tracked as ROADMAP.md P11.

## GPU status

Real, measured, production-path GPU acceleration as of this release (see
"Major changes" above) — a substantive change from v0.1.5's single-kernel,
net-slower GPU path. GPU hardware verification for this release was
performed on WSL2/Linux; native-Windows+CUDA has a CUDA toolkit available on
the test machine but has not itself been build/test verified — the packaged
Windows release remains CPU-only (`CFDAPP_ENABLE_CUDA` off by default).

## Known limitations

- Species/multiphase/compressible physics remain validated at the equation
  level but not yet reachable through `physics.json`/`ProjectRunner`'s
  production dispatch — tracked as `ROADMAP.md` P10.
- No GUI case-authoring (mesh/physics/BC/solver editors) — tracked as
  `ROADMAP.md` P11.
- Native Windows + CUDA is untested; GPU verification for this release was
  performed on WSL2/Linux only.
- Five non-fatal QML console warnings (`Unable to assign [undefined] to
  bool` in `PhysicsEditor.qml`/`BoundaryEditor.qml`) appear on GUI startup
  before any case is open; the app stays alive and fully functional. Found
  during this release's own packaged-smoke-test pass, not by the 17-step
  manual checklist (which only opens an existing case). Worth a follow-up
  QML binding-guard fix, not a release blocker.
- 2D only; structured/Cartesian mesh only.

## Major next steps

1. Confirm this GitHub Release (v0.2.0) is genuinely published with its ZIP,
   NSIS installer, and checksums attached.
2. `ROADMAP.md` P10: wire species/multiphase/compressible physics into
   `physics.json` parsing and `ProjectRunner`'s production dispatch.
3. `ROADMAP.md` P11: GUI case authoring (mesh/physics/BC/solver editors).
