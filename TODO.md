# CFDApp — TODO

> `[x]` = implemented + verified.
> Evidence: `results/` · Rules: `CLAUDE.md` · Direction: `ROADMAP.md`

---

# Status

```text
FINISHED
  P0–P11
  P12-COMP-001/002
  P12-NUM-001–007
  P12-MESH-001–007
  P12-ASAN-001
  P12-DIFF-002
  P12-GRAD-002
  CUDA-QUAL-001
  GPU-PCORR-001

  Exact-SHA CI — PASSED 12/12
  KNOWN-GOOD BASELINE
  c1355eaa77a1b98e37926d20f262b6dae76e45c5

PROCESSING
  None

NEXT
  CI-PERF-001
  GPU-PIPE-001
  P13

AWAITING REVIEW
  AGENT-INFRA-001 (.claude/skills/, uncommitted)

AUTHORIZED NEW DEVELOPMENT
  None
```

---

# 1. Finished

## Core

* [x] P0–P5 — core CFD
* [x] P6 — GPU foundation
* [x] P7 — performance validation
* [x] P8 — production hardening
* [x] P9 — v0.2.0
* [x] P10 — production physics
* [x] P11 — GUI case authoring

## P12

* [x] COMP-001 — EOS boundary density
* [x] COMP-002 — coupled compressible SIMPLE
* [x] NUM-001–007 — numerics
* [x] MESH-001 — non-orthogonal meshes
* [x] MESH-002 — stretched meshes
* [x] MESH-003 — 2D multi-block
* [x] MESH-004 — mesh quality
* [x] MESH-005 — 3D foundation
* [x] MESH-006 — 3D SIMPLE
* [x] ASAN-001 — sanitizer repair
* [x] DIFF-002 — boundary diffusion
* [x] GRAD-002 — Green–Gauss treatment
* [x] MESH-007 — moving-mesh/ALE foundation

## CUDA

* [x] **CUDA-QUAL-001**

  * CUDA 12.9.86
  * RTX 5000 Ada / CC 8.9
  * native `sm_89`
  * 83/83 GPU CTest
  * compute-sanitizer clean
  * CPU/GPU equivalence passed

* [x] **GPU-PCORR-001**

  * fixed GPU BiCGSTAB scale-dependent breakdown
  * 160² / 320² / 640² pass
  * 320²: 1.33× GPU
  * 640²: 3.20× GPU
  * negative control passed

Evidence:

```text
results/cuda-qual-001/
results/gpu-pcorr-001/
```

---

# 2. Processing

**None.**

## Exact-SHA CI — PASSED

```text
SHA: c1355eaa77a1b98e37926d20f262b6dae76e45c5
Run: 35352132866   2026-09-18 13:45Z → 18:22Z   12/12 success

python · format · clang-tidy · GCC Release · GCC Debug · Clang Debug
sanitizers ×5 · sanitizer-coverage

1932/1932 tests pass in each of GCC Release, GCC Debug and Clang Debug
sanitizer shards 389+387+388+384+384 = 1932, 0 failures
coverage audit: full list 1977, union 1977 distinct — each test in exactly one shard

GATE MET → c1355ea IS THE KNOWN-GOOD BASELINE
```

GitHub runners have **no GPU**: this run does not validate `sm_89` execution.
GPU evidence is local only — `results/cuda-qual-001/`, `results/gpu-pcorr-001/`.

## AGENT-INFRA-001 — awaiting review

```text
.claude/skills/ (10 skills) · docs/AGENT_WORKFLOW.md · CLAUDE.md section
evidence: results/agent-infra-001/
uncommitted; no production code, numerics, CUDA or CI touched
```

---

# 3. Next

## CI-PERF-001 — Faster CI — BLOCKED (needs a push to measure)

* [x] Profile build/test time — `results/ci-perf-001/baseline/`
* [x] Balance shards using test runtimes — LPT, spread 1.00x on all three configs
* [x] Add coverage audit — 6/6 injected defects detected
* [x] Investigate compiler caching — ccache added, keyed per compiler+build type
* [ ] Shard GCC Debug — implemented + locally verified; unproven in real CI
* [ ] Shard Clang Debug — implemented + locally verified; unproven in real CI
* [ ] Preserve all tests and sanitizers — locally 1984/1984; unproven in real CI

```text
Baseline   276.3 min (4 h 36 min)   run 35352132866
Predicted   ~85 min                 3.2x, floor-bound
Goal        <120 min
```

Root causes found: the `build-test` test step ran **serially** (no `-j`), and sharding balanced
test **count** rather than **runtime** (65.5-101.2 min sanitizer spread).

Floor: `CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic`, 4838 s under ASan.
No partition beats it, so ~85 min is the limit without a production change.

**Blocked on:** authorization to commit + push, then one CI run. The gate's
"actual optimized CI evidence" and "<2 h achieved" cannot be met without it, and are
not being marked met. Evidence: `results/ci-perf-001/summary.md`.

---

## GPU-PIPE-001 — Persistent GPU Pipeline

* [ ] Measure H2D/D2H transfers
* [ ] Persistent GPU fields
* [ ] Persistent matrices
* [ ] Remove unnecessary transfers
* [ ] GPU-resident pressure solve
* [ ] GPU-resident SIMPLE loop
* [ ] CPU/GPU equivalence
* [ ] CUDA diagnostics
* [ ] 20²–640² benchmarks
* [ ] Full regression

Motivation:

```text
160²:
~71,031 downloads
~544 uploads

resident SpMV:
~51× CPU
```

---

## P13 — Production Maturity

* [ ] Restart/checkpointing
* [ ] Robust case validation
* [ ] CLI/GUI consistency
* [ ] Results/export
* [ ] Diagnostics/logging
* [ ] Packaging
* [ ] Production benchmark suite

---

# 4. Future

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
* [ ] API/case-format stability
* [ ] Cross-platform qualification
* [ ] Documentation
* [ ] Packaging
* [ ] v1.0 release

---

# 5. Technical Debt

Do not fix without authorization.

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
* [ ] GPU transfer overhead
* [ ] GUI `-Wconversion` warnings
* [ ] Native Windows CUDA qualification

---

# 6. Execution Order

```text
P0–P11                  ✓
   ↓
P12                      ✓
   ↓
CUDA-QUAL-001            ✓
   ↓
GPU-PCORR-001            ✓
   ↓
Exact-SHA CI             ✓
   ↓
Known-good baseline      ✓  c1355ea
   ↓
CI-PERF-001              ◉ PROCESSING
   ↓
GPU-PIPE-001
   ↓
P13
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
4. `[x]` requires real evidence.
5. Preserve failures and negative controls.
6. Correctness before performance.
7. Do not fix unrelated debt.
8. No new phase without authorization.
9. No commit/push without authorization.
10. Keep detailed evidence in `results/`, not `TODO.md`.
