# CFDApp — TODO

> **Active execution dashboard.**
>
> Rules: [CLAUDE.md](CLAUDE.md) · Direction: [ROADMAP.md](ROADMAP.md) · Evidence: [results/](results/)
>
> `[x]` = implemented **and** verified with real evidence. Everything else stays `[ ]`.

```text
COMPLETED THROUGH:  the whole P12 lineage -- committed, pushed and CI-green on 67b9e3e

CURRENT:            CUDA-QUAL-001 -- STOPPED at a failed gate (CPU/GPU equivalence at
                    production scale); toolchain work itself passed, uncommitted

NEXT:               user decision on the CUDA-QUAL-001 gate failure
                    (no other phase is authorized)

PUSH BLOCKER:       none in tests; CUDA-QUAL-001's changes are uncommitted
                    commit/push requires explicit authorization
```

| State                  | Value                                                                                                   |
| ---------------------- | ------------------------------------------------------------------------------------------------------- |
| Release                | `v0.2.0` (`1e960c7`)                                                                                    |
| Last pushed            | `67b9e3e` (P12 lineage + CI repair), CI run 35313512418 green on that exact SHA, 12/12 jobs             |
| Working tree           | clean except CUDA-QUAL-001's two build files and its evidence, all uncommitted                          |
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

## CUDA-QUAL-001 — Ada CUDA toolchain qualification

**Status: 🔴 INCOMPLETE — 13/15 acceptance items passed; BLOCKED on production-scale CPU/GPU SIMPLE
equivalence (2026-09-18). Checkpointed locally, not pushed.**

13 of 15 acceptance items pass. It stops at **CPU/GPU equivalence at production scale**: the GPU
backend cannot complete a SIMPLE solve at 320² or 640² (`PressureCorrectionFailure`, 0 outer
iterations) where the CPU backend completes. The failure is **not** caused by this phase — it
reproduces identically on the historical CUDA 11.5 / sm_52 toolchain — and the GPU linear solvers
are healthy at that same size, so it lies in the production pressure-correction path.

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
was sm_52 + PTX. Fixed in `cmake/CUDA.cmake` (uncommitted).

Evidence: `results/cuda-qual-001/summary.md`.

* [ ] Resolve the 320²/640² `PressureCorrectionFailure` (needs its own authorization)
* [ ] Re-run acceptance item 9 afterwards; items 1–8 and 10–15 stand on the evidence above
* [ ] Commit / push decision for the toolchain change

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

* [ ] **GPU backend fails a production solve at 320² and above**

  * `PressureCorrectionFailure` on the first outer iteration, 0 iterations completed, where the CPU
    backend completes normally; a regression against the P7 evidence, which recorded 320² on the GPU
    converging at 2.0× speed-up;
  * **not** toolchain-related: identical on CUDA 11.5/sm_52 and 12.9/sm_89, and the GPU CG and
    BiCGSTAB both converge and match the CPU at that exact size;
  * blocks CUDA-QUAL-001's equivalence gate; needs its own authorization (found 2026-09-18).

Evidence:

```text
results/cuda-qual-001/summary.md  section 7
```

* [ ] **GPU end-to-end performance is transfer-bound and below the CPU**

  * at every grid where the GPU backend completes (20²–160²) it is 1.7× to 45× slower than the CPU,
    with 33–45 % of its time in host↔device transfer and a per-iteration device→host round trip
    (71 031 downloads against 544 uploads at 160²);
  * the SpMV kernel itself reaches 51× over the CPU when data stays resident, so the cost is the
    transfer pattern, not the kernel;
  * no crossover is demonstrated; optimization is a separate, unauthorized phase.

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

## Production

* [ ] **P13 — Production Maturity**

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
