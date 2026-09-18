# CFDApp — TODO

> **Active execution dashboard.**
>
> Rules: [CLAUDE.md](CLAUDE.md) · Direction: [ROADMAP.md](ROADMAP.md) · Evidence: [results/](results/)
>
> `[x]` = implemented **and** verified with real evidence. Everything else stays `[ ]`.

```text
COMPLETED THROUGH:  the whole P12 lineage (pushed, exact-SHA CI green on 67b9e3e)
                    CUDA-QUAL-001  COMPLETE
                    GPU-PCORR-001  COMPLETE

CURRENT:            none

NEXT:               user scope decision
                    GPU-PIPE-001 is a FUTURE candidate, NOT AUTHORIZED
                    P13 NOT AUTHORIZED

PUSH BLOCKER:       none in tests
                    commit/push requires explicit authorization, each time
```

| State                  | Value                                                                                                   |
| ---------------------- | ------------------------------------------------------------------------------------------------------- |
| Release                | `v0.2.0` (`1e960c7`)                                                                                    |
| Last pushed            | `67b9e3e` (P12 lineage + CI repair), CI run 35313512418 green on that exact SHA, 12/12 jobs             |
| CUDA work              | `999d6a3` (CUDA-QUAL-001 toolchain) and the GPU-PCORR-001 repair commit, pushed together after it       |
| Latest full regression | on `67b9e3e` in CI: GCC Release 1932/1932, GCC Debug 1932/1932, Clang Debug 1932/1932, sanitizers 1932 enabled tests across 5 shards, 0 ASan/UBSan/LSan diagnostics |
| Historical TODO        | `results/todo-archive/TODO-2026-09-17-pre-restructure.md`                                               |

**Authorized chain (2026-09-17, complete):**

```text
DIFF-002 → GRAD-002 → MESH-007   ✓ committed + pushed + exact-SHA CI green
```

Do not start Technical Debt or Future work without explicit authorization.

---

# 1. Completed

## Foundation and Releases

* [x] **P0–P5** — core CFD (`v0.1.5`)
* [x] **P6** — GPU performance
* [x] **P7** — performance validation
* [x] **P8** — production hardening
* [x] **P9** — `v0.2.0` release
* [x] **P10** — production physics integration
* [x] **P11** — GUI case authoring

## P12 — Compressible

* [x] **P12-COMP-001** — EOS boundary density
* [x] **P12-COMP-002** — coupled compressible SIMPLE

## P12 — Numerics

* [x] **P12-NUM-001–007** — numerical methods and robustness

  * committed
  * closeout: `results/p12-num-closeout/summary.md`

## P12 — Mesh Foundation

* [x] **P12-MESH-001** — production non-orthogonal structured meshes
* [x] **P12-MESH-002** — stretched / graded meshes
* [x] **P12-MESH-003** — general 2D multi-block geometry
* [x] **P12-MESH-004** — mesh quality and validation
* [x] **P12-MESH-005** — 3D mesh and operator foundation
* [x] **P12-MESH-006** — 3D incompressible SIMPLE under Amendment A3

MESH-001–006 are verified but uncommitted.

Evidence:

```text
results/p12-mesh-001/
...
results/p12-mesh-006/
```

## P12 — Boundary Diffusion

* [x] **P12-DIFF-002** — second-order Dirichlet boundary diffusion

Verified 2026-09-17:

```text
W8B  PASS
W9A  PASS
W10  PASS
```

W10:

```text
Release:       1923/1923
Debug + GUI:   1975/1975
ASan + UBSan:  1923/1923
Diagnostics:   0
```

Evidence:

```text
results/p12-diff-002/summary.md
```

## P12 — Sanitizer Fix

* [x] **P12-ASAN-001** — MESH-004 test heap-use-after-free

Test-only fix; production behavior unchanged.

Evidence:

```text
results/p12-asan-001/summary.md
```

## P12 — Gradient

* [x] **P12-GRAD-002** — continuous Green–Gauss boundary treatment

Verified 2026-09-17 under Amendments A1, A2 and A3:

```text
A3 fresh gate     PASS  (C2-A2, C8–C13; the A1-form criteria rerun on the final library)
Release:          1932/1932
Debug + GUI:      1984/1984
ASan + UBSan:     1932/1932, 0 diagnostics
clang-format:     0 of 568
```

Failures kept on record: GRAD-001; the original gate (C1); A1 (3D deformed); A2 (C10-A2(d));
INV-001's findings; the INVALID and FAIL-CLOSED run logs.

Evidence:

```text
results/p12-grad-002/summary.md
results/p12-grad-002/a3/summary_a3.md
```

## P12 — Moving Mesh

* [x] **P12-MESH-007** — moving / deforming mesh foundation

Scope, by user decision: a **library-level C++ API**, not a production capability (CLAUDE.md §9).

* 2D ALE/PISO is a C++ API only; the case format, CLI and GUI stay steady-SIMPLE only.
* 3D covers geometry, swept volumes, the GCL and the ALE operators only.

Verified 2026-09-17:

```text
G6.3 / G7.3 unchanged rerun   PASS  1.900e-13 ≤ 1e-8   (original FAIL 2.491e-02, kept)
G2.3                          PASS
G9 (Amendment A3)             PASS  dry-run → freeze → fresh
G10.1                         PASS  (fresh)
G10.2–G10.4                   PASS  (the GRAD-002 A3 regression, same build inputs, disclosed)
performance baseline          recorded
```

Failures kept on record: the original G6.3 / G7.3 (log 12); the invalid and failed G9 dry-run
attempts; log 23's instrument-bug verdict.

Evidence:

```text
results/p12-mesh-007/summary.md
```

---


## CUDA-QUAL-001 — Ada CUDA toolchain qualification

**Status: ✅ COMPLETE (2026-09-18) — all 15 acceptance items pass.**

Items 1–8 and 10–15 passed at the checkpoint. Item 9, production-scale CPU/GPU equivalence, failed
there (`PressureCorrectionFailure` at 320² and 640²) and was unblocked by GPU-PCORR-001; the
authoritative end-to-end benchmark now completes on the GPU at every grid. Gate resolution recorded
in `results/cuda-qual-001/summary.md` §13, which leaves the original failure text intact.

Verified and passing:

```text
CUDA 12.9.86 active (no driver package touched)   sm_89 cubin present in the built library
independent smoke kernel: __CUDA_ARCH__ 890       clean build, 0 warnings
real GPU execution, CPU fallback ruled out        83/83 GPU ctest, 66/66 GPU unit tests
equivalence <= 1.6e-9 relative, NaN/Inf 0         determinism bitwise
compute-sanitizer memcheck/initcheck/synccheck/racecheck: 0 errors
CPU regression green: Release 1932/1932, Debug + GUI 1984/1984
```

Also found: the documented sm_80 target had never applied — `enable_language(CUDA)` sets the
architecture from nvcc's default (52) before `cuda/CMakeLists.txt`'s guard could, so every GPU build
was sm_52 + PTX. Fixed in `cmake/CUDA.cmake` (committed at `999d6a3`).

Evidence: `results/cuda-qual-001/summary.md`.

* [x] Resolve the 320²/640² `PressureCorrectionFailure` — GPU-PCORR-001 below
* [x] Re-run acceptance item 9 — the end-to-end benchmark completes on the GPU at every grid

## GPU-PCORR-001 — production GPU pressure-correction recovery

**Status: ✅ COMPLETE (2026-09-18).**

Root cause: `GpuBiCGSTAB`'s breakdown tests were **absolute** (`|rho| < constants::tiny = 1e-30`),
while the CPU's have been **scale-relative** since P12-MESH-004 (`cancelledToRoundingLevel`). The
pressure-correction residual shrinks with the grid, so at 320² a healthy iteration was read as a
breakdown. Not the toolchain: the historical CUDA 11.5 / sm_52 build fails identically.

Minimal fix, GPU only: `absDot` (a device reduction over term magnitudes) plus the CPU's criteria in
`GpuBiCGSTAB`. `GpuCG` is deliberately untouched — its absolute test matches the CPU's and is the
separate "CG absolute breakdown threshold" debt item.

```text
160^2 / 320^2 / 640^2 GPU   PASS, ran_cleanly yes, residuals identical to CPU
negative control            absolute test restored -> the same failure at the same 876 iterations
end-to-end benchmark        every grid clean; real 1.33x at 320^2 and 3.20x at 640^2
GPU ctest 83/83, GPU units 66/66, CPU solver units 42, sanitizers 0 errors (4 tools)
CPU regression              Release 1932/1932, Debug + GUI 1984/1984, clang-format clean
```

Known remaining asymmetry, not fixed: the CPU restarts its Krylov sequence on cancellation; the GPU
reports Breakdown. Unreachable in the measured cases now that the criterion no longer misfires.

Evidence: `results/gpu-pcorr-001/summary.md`.

* [ ] Commit / push decision (no commit authorized yet)

---

## Closed Without Completion

Historical only. Do not resume without authorization.

* [ ] **P12-GRAD-001**

  * failed its gate;
  * superseded by GRAD-002;
  * evidence: `results/p12-grad-001/`

* [ ] **P12-DIFF-001**

  * premise disproved before gate freeze;
  * superseded by DIFF-002;
  * evidence: `results/p12-diff-001/`

---

# 2. Active

None. CUDA-QUAL-001 and GPU-PCORR-001 are complete; see section 1.

---

# 3. Next

* [x] **Commit / push of the verified P12 lineage** — done 2026-09-18. `67b9e3e` on `origin/main`;
  CI run 35313512418 green on that exact SHA (12/12 jobs). The lineage (MESH-001 … MESH-007,
  DIFF-002, GRAD-002, ASAN-001) and a CI portability repair are committed, pushed and CI-qualified.
* [ ] **User scope decision** after CUDA-QUAL-001's gate failure. No phase is authorized.

---

# 4. Known Technical Debt

**Recorded, not authorized.**

Do not fix these opportunistically inside another phase.

* [ ] **2D default Linear face flux**

  * undamped odd-even pressure mode on open domains;
  * SIMPLE converges slowly;
  * results may depend on stopping point;
  * W8 cases explicitly use Rhie–Chow;
  * changing the 2D default requires a separate decision.

Evidence:

```text
results/p12-grad-002/drift-001/summary.md
```

* [ ] **Green–Gauss fixed four-sweep loop**

  * GRAD-002 increases four-sweep truncation by up to ~4.2×;
  * negligible on committed meshes;
  * does not vanish under refinement on uniformly tilted 3D boundaries;
  * classified as debt, not a blocker (GRAD-002 A3 §5).

* [ ] **Fine-grid instability**

  * one-correction recipe on smooth non-orthogonal meshes.

* [ ] **Explicit `diffusion()` two-cell defect**

  * incorrect with exactly two cells along a boundary normal.

* [ ] **CG absolute breakdown threshold**

  * BiCGSTAB is scale-invariant;
  * CG is not.

* [ ] **Irregular-mesh convergence**

  * velocity/pressure do not converge on cell-scale irregular meshes;
  * uncorrected recipe diverges on smooth non-orthogonal meshes.

* [ ] **Warped 3D faces**

  * one stored centroid cannot exactly represent a warped-face integral;
  * linear-field error is O(h²).

* [ ] **Large-coordinate geometry**

  * `createStructuredQuad2D` breaks down beyond approximately `X/h ≈ 1e3`.

* [ ] **Sheared-mesh asymmetry**

  * wall faces corrected;
  * internal faces not equivalently corrected.

* [ ] **Species lagged-boundary pattern**

  * inherited from MESH-003;
  * needs re-evaluation.

* [ ] **3D boundary-condition lookup performance**

  * O(boundary faces);
  * approximately 25–30% runtime at `64³`.

* [ ] **Six `-Wconversion` GUI warnings**

  * `apps/gui/SimulationControllerEditing.cpp:86-89`

* [ ] **Native Windows + CUDA validation**

  * WSL2 + CUDA verified;
  * native Windows remains manual/unverified.

* [ ] **CUDA toolchain vs GPU** — addressed by CUDA-QUAL-001, committed at its checkpoint, not pushed

  * was: WSL nvcc 11.5 and sm_52 against an Ada (8.9) GPU, so every GPU build ran by PTX JIT;
  * now: CUDA 12.9.86 and architectures `80;89`, with sm_89 cubin proven present in the built
    library; the architecture choice moved before `enable_language(CUDA)`, which is why the old
    sm_80 default never applied;
  * evidence: `results/cuda-qual-001/{toolchain,build}.md`.

* [x] **GPU backend fails a production solve at 320² and above** — fixed by GPU-PCORR-001,
  **uncommitted**

  * was `PressureCorrectionFailure` on the first outer iteration where the CPU completed; a
    regression against P7's recorded 320² GPU run;
  * cause was `GpuBiCGSTAB`'s absolute breakdown threshold, not the toolchain (it reproduced on
    CUDA 11.5 / sm_52 too);
  * evidence: `results/gpu-pcorr-001/summary.md`, `results/cuda-qual-001/summary.md` §7 and §13.

* [ ] **GPU BiCGSTAB has no Krylov restart, the CPU does**

  * on detecting cancellation the CPU restarts the sequence and only reports Breakdown if that
    cannot help; the GPU reports Breakdown immediately;
  * unreachable in the cases measured after GPU-PCORR-001, but still a real difference from the CPU
    solver (found 2026-09-18).

* [ ] **GPU end-to-end performance is transfer-bound below the crossover**

  * the GPU backend is slower than the CPU at 20²–160² (0.02×–0.51×), with 35–46 % of its time in
    host↔device transfer and a per-iteration device→host round trip (71 031 downloads against 544
    uploads at 160²);
  * the crossover is at 320² (1.33×), reaching 3.20× at 640² once transfer falls to 12 %;
  * the SpMV kernel itself reaches 51× over the CPU when data stays resident, so the cost is the
    transfer pattern, not the kernel;
  * future scope candidate **GPU-PIPE-001 — persistent GPU-resident production pipeline**; only
    after production GPU correctness is committed, and never mixed with a correctness fix.

---

# 5. Future — Not Authorized

ROADMAP.md owns the detailed rationale and sequencing.

Being listed here is **not authorization**.

## Physics

* [ ] P12-COMP follow-ups

  * compressible energy coupling
  * higher-Mach capability

* [ ] P12-TURB

  * DNS-backed turbulence validation
  * wall treatment

* [ ] P12-SPECIES

  * multi-species
  * reactions
  * source terms

* [ ] P12-MULTI

  * interface methods
  * surface tension

## Numerics

* [ ] 2D Rhie–Chow default decision
* [ ] Multigrid
* [ ] Stronger preconditioners
* [ ] Coupled solver

## 3D

* [ ] Transient 3D
* [ ] Thermal 3D
* [ ] Turbulent 3D
* [ ] Non-Cartesian 3D meshes
* [ ] Parallel 3D

## Moving Mesh (MESH-007 follow-ups)

* [ ] Case-format, CLI and GUI integration of mesh motion and transient PISO
* [ ] 3D ALE flow solver; moving-mesh MMS; time-dependent moving walls; moving-mesh restart

## GPU

* [ ] **GPU-PIPE-001 — persistent GPU-resident production pipeline** — NOT AUTHORIZED

  * motivation, measured by CUDA-QUAL-001 and GPU-PCORR-001: the GPU backend is slower than the CPU
    below the 320² crossover, 33–46 % of its time is host↔device transfer, and at 160² it makes
    ~71 031 downloads against ~544 uploads, while the resident SpMV kernel reaches ~51×;
  * so the next bottleneck is host/device traffic, not GPU compute;
  * only after the production GPU correctness work is committed, and never mixed with a correctness
    fix.

## Production

* [ ] **P13 — Production Maturity** — NOT AUTHORIZED

---

# 6. Execution Order

```text
COMPLETED
│
├─ P0–P11                         ✓
├─ P12-COMP-001/002               ✓
├─ P12-NUM-001–007                ✓
├─ P12-MESH-001–006               ✓
├─ P12-ASAN-001                   ✓
├─ P12-DIFF-002                   ✓
├─ P12-GRAD-002                   ✓ (A3)
└─ P12-MESH-007                   ✓ (C++ API; G9 under A3)
        │
        ▼
COMMIT / PUSH DECISION            ← NEXT (user)
        │
        ▼
NEXT USER-AUTHORIZED PHASE
```

---

## Rules

1. Work top-to-bottom.
2. Stop at the first legitimate failed gate.
3. Never weaken a threshold merely to pass.
4. Preserve failed gates and amendments as evidence.
5. `[x]` requires implementation **and real verification**.
6. Use fresh binaries for authoritative regression evidence.
7. Dry-run new numerical gate instruments and prove non-vacuity before freezing.
8. Do not fix Technical Debt opportunistically.
9. Do not start a new major phase without explicit authorization.
10. Do not commit or push without explicit authorization.
