# CFDApp — TODO

> **Active execution dashboard.**
>
> Rules: [CLAUDE.md](CLAUDE.md) · Direction: [ROADMAP.md](ROADMAP.md) · Evidence: [results/](results/)
>
> `[x]` = implemented **and** verified with real evidence. Everything else stays `[ ]`.

```text
COMPLETED THROUGH:  P12-DIFF-002 + P12-ASAN-001 + P12-GRAD-002 + P12-MESH-007

CURRENT:            none -- the authorized chain DIFF-002 → GRAD-002 → MESH-007 is complete

NEXT:               user decision: commit / push of the verified P12 lineage
                    (no phase is authorized)

PUSH BLOCKER:       none known in tests
                    commit/push requires explicit authorization
```

| State                  | Value                                                                                                   |
| ---------------------- | ------------------------------------------------------------------------------------------------------- |
| Release                | `v0.2.0` (`1e960c7`)                                                                                    |
| Last pushed            | P12-NUM `105383d` + CI fix `44b996a`, CI green                                                          |
| Working tree           | P12-MESH-001 onward is **uncommitted**. No commit or push authorized.                                   |
| Latest full regression | GRAD-002 A3: Release 1932/1932, Debug + GUI 1984/1984, ASan + UBSan 1932/1932, 0 sanitizer diagnostics  |
| Historical TODO        | `results/todo-archive/TODO-2026-09-17-pre-restructure.md`                                               |

**Authorized chain (2026-09-17):**

```text
DIFF-002 → GRAD-002 → MESH-007
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

None. The authorized chain DIFF-002 → GRAD-002 → MESH-007 is complete (2026-09-17).

---

# 3. Next

* [ ] **Commit / push decision** for the verified P12 lineage (MESH-001 … MESH-007, DIFF-002,
  GRAD-002, ASAN-001). A green CI run on the exact SHA is required. This needs the user's explicit
  authorization; no phase is authorized.

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

* [ ] **CUDA toolchain vs GPU**

  * WSL has nvcc 11.5 and `CMAKE_CUDA_ARCHITECTURES=52`, but the GPU is Ada (compute capability 8.9);
  * upgrade the toolkit and architecture before producing GPU performance evidence (found in an environment check, 2026-09-17).

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
