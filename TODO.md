# CFDApp — TODO

> `[x]` = implemented **and verified**
> Evidence: `results/` · Rules: `CLAUDE.md` · Direction: `ROADMAP.md`

---

# Status

```text
FINISHED
  P0–P12
  CUDA-QUAL-001
  GPU-PCORR-001
  AGENT-INFRA-001
  CI-PERF-001
  GPU-DISC-001 — CUDA discretization pipeline
  GPU-PIPE-001 — final GPU residency

KNOWN-GOOD
  548401aef0d3cbab9552b1964ceac7efd7ad6154
  CI 35385979133 — 17/17 PASS — 100.6 min

CURRENT
  nothing in flight

NEXT
  P13 — Production Maturity (not started, NOT AUTHORIZED)

PUSH BLOCKER
  none outstanding. The GPU-DISC-001 + GPU-PIPE-001 lineage is uncommitted
  (16 tracked files modified, 50 untracked) and has never been through CI.
  results/validation/** carries timing-only churn from the ctest runs and
  must be restored before any commit — 0 non-timing changes, verified.
```

---

# 1. Completed Milestones

## Core

* [x] P0–P5 — core CFD
* [x] P6 — GPU foundation
* [x] P7 — performance validation
* [x] P8 — production hardening
* [x] P9 — v0.2.0 release
* [x] P10 — production physics
* [x] P11 — GUI case authoring
* [x] P12 — advanced numerics / mesh / compressible foundations

## P12 Closeout

* [x] COMP-001/002 — compressible SIMPLE
* [x] NUM-001–007 — numerics
* [x] MESH-001–007 — mesh + 3D + ALE
* [x] ASAN-001 — sanitizer repair
* [x] DIFF-002 — boundary diffusion
* [x] GRAD-002 — Green–Gauss treatment

## CUDA / Infrastructure

* [x] CUDA-QUAL-001 — RTX 5000 Ada / `sm_89`
* [x] GPU-PCORR-001 — GPU BiCGSTAB pressure-correction qualification
* [x] AGENT-INFRA-001 — repository-local agent workflow
* [x] CI-PERF-001 — CI critical path 276.3 → 100.6 min

Evidence:

```text
results/cuda-qual-001/
results/gpu-pcorr-001/
results/agent-infra-001/
results/ci-perf-001/
```

> GitHub CI has no GPU. CUDA execution evidence is local.

---

# 2. GPU-DISC-001 — CUDA Discretization Pipeline

## Foundation

* [x] Device mesh / geometry

## Operators

* [x] Gradients — CUDA Green–Gauss; bitwise CPU/GPU match
* [x] Diffusion — CUDA finite-volume diffusion
* [x] Convection — production scalar/vector schemes
* [x] Boundary conditions — reusable CUDA scalar/vector BC layer

## Momentum Path

* [x] Momentum assembly
* [x] Momentum response coefficients
* [x] Rhie–Chow / predicted face flux

## Pressure Path

* [x] Pressure-correction assembly
* [x] Velocity correction
* [x] Face-flux correction

## Integration

* [x] Single-iteration CPU/GPU differential
* [x] Integrated GPU SIMPLE discretization
* [x] Full-solve CPU/GPU equivalence
* [x] CUDA diagnostics
* [x] Negative controls
* [x] Performance qualification
* [x] Full regression

Evidence:

```text
results/gpu-disc-001/
```

**Status: FINISHED**

---

# 3. GPU-PIPE-001 — Persistent GPU Pipeline

## Foundations

* [x] Transfer instrumentation / baseline
* [x] BiCGSTAB reduction fusion

  * 7 → 5 reductions / Krylov iteration
* [x] Synchronization reduction

  * sync count −66.7%
* [x] Persistent matrices / workspaces

  * 0 steady-state reallocations
* [x] CUDA diagnostics
* [x] 20²–640² benchmark ladder
* [x] Full regression

Historical performance:

```text
160²   0.702× GPU/CPU
320²   1.773×
640²   3.599×
```

Evidence:

```text
results/gpu-pipe-001/
```

## Final Residency Implementation

* [x] Persistent GPU fields

  * production SIMPLE state persists on device across iterations

* [x] GPU-resident pressure solve

  * pressure matrix remains on device — solved in place, adopted not copied
  * RHS remains on device
  * `p'` remains on device
  * persistent Krylov workspace reused — 0 steady-state allocations
  * per solve: 0 H2D, 0 assemble bytes, 2 non-reduction D2H (12 bytes of
    host *decisions*); the host matrix rebuild is gone
  * bitwise equivalent, including a 60-iteration long run
  * 640² 9.282 s → 8.189 s; GPU speedup 9.72× → 11.40×

* [x] GPU-resident SIMPLE loop

  * momentum assembled and solved on the device; predictor never leaves it
  * steady-state H2D: 0 calls, 0 bytes — 2D and 3D, every grid
  * non-reduction D2H: 8/iteration (2D), 9 (3D); besides the face flux, 40 bytes
  * 0 steady-state allocations, 0 reallocations
  * bitwise vs the pre-residency path: 12 cases, 217,755 values, 2 long runs
  * transfer gate failed once on a mis-derived criterion — amendment A1

* [x] Final CPU/GPU equivalence

  * 15 converged/classified cases + a 160²/320²/640² fixed-budget ladder
  * every discrepancy ≤ 9.6e-10 against the adopted 1e-6 bound
  * equal outer-iteration counts on every acceptance case
  * determinism bitwise on both arms
  * known BiCGSTAB reproducer UNCHANGED at exactly 1845 iterations

Evidence:

```text
results/gpu-pipe-001/final-residency/
  audit.md · summary.md · acceptance_gate_A1.md
  transfers/FAILURE.md — the transfer gate's failure, preserved
```

**Status: FINISHED**

---

# 4. GPU-PIPE-001 — Final Qualification

> Run only after all three residency implementation items above are complete.

* [x] Persistent GPU fields — final-gate re-verification
* [x] GPU-resident pressure solve — final-gate re-verification
* [x] GPU-resident SIMPLE loop — final-gate re-verification
* [x] Dedicated CPU/GPU equivalence
* [x] CUDA diagnostics — 16/16, 0 errors / 0 hazards
* [x] Final 20²–640² benchmark — 640² 3.593× → 17.362×; crossover 320² → 160²
* [x] Full regression

  * Release+CUDA 1998/1998 · Debug+GUI 1984/1984 · ASan/UBSan 1932/1932 (0 reports)
  * CPU-only 1932/1932 · 15/15 GPU-DISC gates · production CLI smoke · determinism
  * generated outputs vs HEAD: 3014 files, **0 non-timing changes**
  * clang-format: 0 violations in 590 files. 105 pre-existing violations in 12
    GPU-DISC files were cleared; libcfdcore.a is byte-identical afterwards, and
    CTest, the 15 gates and the bitwise comparison were re-run on the result.

## Final Gate

GPU-PIPE-001 closes only when:

```text
Correctness           PASS
CPU/GPU equivalence   PASS
CUDA diagnostics      PASS
Transfer audit        PASS
Residency audit       PASS
Performance           PASS
Full regression       PASS
```

Then:

```text
GPU-PIPE-001 → FINISHED
P13          → PROCESSING
```

---

# 5. P13 — Production Maturity

* [ ] Restart / checkpointing
* [ ] Robust case validation
* [ ] CLI / GUI consistency
* [ ] Results / export
* [ ] Diagnostics / logging
* [ ] Packaging
* [ ] Production benchmark suite

---

# 6. Future CFD Roadmap

## Physics

* [ ] Compressible energy / higher Mach
* [ ] Turbulence
* [ ] Species / reactions
* [ ] Multiphase

## Numerics

* [ ] Rhie–Chow default
* [ ] Multigrid
* [ ] Stronger preconditioners
* [ ] Coupled solver

## 3D

* [ ] Transient
* [ ] Thermal
* [ ] Turbulent
* [ ] Non-Cartesian
* [ ] Parallel

## Moving Mesh

* [ ] Case-format integration
* [ ] CLI / GUI
* [ ] Production 3D ALE
* [ ] Moving-mesh MMS
* [ ] Restart

## HPC

* [ ] OpenMP scaling
* [ ] MPI
* [ ] Multi-GPU
* [ ] Large-grid optimization

## v1.0

* [ ] V&V benchmark library
* [ ] API / case-format stability
* [ ] Cross-platform qualification
* [ ] Documentation
* [ ] Packaging
* [ ] v1.0 release

---

# 7. Technical Debt

> Do not fix without explicit authorization.

* [ ] CG absolute breakdown threshold
* [ ] GPU BiCGSTAB restart asymmetry
* [ ] 2D Linear face-flux pressure mode
* [ ] Green–Gauss four-sweep limitation
* [ ] Fine-grid instability
* [ ] Two-cell `diffusion()` defect
* [ ] Irregular-mesh convergence
* [ ] Warped 3D faces
* [ ] Large-coordinate geometry
* [ ] Sheared-mesh asymmetry
* [ ] Species boundary lag
* [ ] 3D BC lookup performance
* [ ] GPU transfer overhead — now the Krylov reductions only; they are the
      dominant cost of the resident path (69–85% of the solve)
* [ ] Benchmark raw-CSV path is hardcoded, so each phase overwrites the last
* [ ] `enableGpuDiscretization` has no case-file key (GPU-DISC-001R finding)
* [ ] GUI `-Wconversion` warnings
* [ ] Native Windows CUDA qualification

## Known GPU BiCGSTAB Asymmetry

Reproducer:

```text
2D cavity:       40×40
outer tolerance: 1e-6
outer budget:    3000

GPU:
PressureCorrectionFailure
BiCGSTAB breakdown after 74 iterations

CPU:
continues full outer budget
```

Present at baseline:

```text
548401aef0d3cbab9552b1964ceac7efd7ad6154
```

Evidence:

```text
results/gpu-pipe-001/equivalence/
```

Do not fix during unrelated milestones.

---

# 8. Execution Order

```text
P0–P12                         ✓
   ↓
CUDA-QUAL-001                  ✓
   ↓
GPU-PCORR-001                  ✓
   ↓
AGENT-INFRA-001                ✓
   ↓
CI-PERF-001                    ✓
   ↓
GPU-PIPE-001 foundations       ✓
   ↓
GPU-DISC-001                   ✓
   ↓
Persistent GPU fields          ✓
   ↓
GPU-resident pressure solve    ✓
   ↓
GPU-resident SIMPLE loop       ✓
   ↓
Final CPU/GPU equivalence      ✓
   ↓
GPU-PIPE-001 final qualification  ✓
   ↓
P13 — Production Maturity      ← NEXT (not authorized)
   ↓
Advanced CFD / HPC
   ↓
v1.0
```

---

# Rules

1. Work top-to-bottom.
2. Stop at a failed gate.
3. Never weaken thresholds to pass.
4. `[x]` requires implementation **and real verification evidence**.
5. Preserve failures and negative controls.
6. Correctness before performance.
7. CPU numerics remain the reference implementation.
8. Do not fix unrelated technical debt.
9. No new phase without authorization.
10. No commit/push without authorization.
11. Keep detailed evidence in `results/`, not `TODO.md`.
12. Implementation completion and final-gate re-verification are separate.
13. Do not claim GPU residency unless transfer evidence proves it.
14. Do not claim performance improvements without equivalent numerical solves.
15. Every final GPU milestone must preserve the CPU backend.
