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
  AGENT-INFRA-001
  CI-PERF-001

  KNOWN-GOOD BASELINE
  548401aef0d3cbab9552b1964ceac7efd7ad6154
  run 35385979133 — 17/17, 100.6 min

PROCESSING
  None

NEXT
  GPU-PIPE-001
  P13

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

## Infrastructure

* [x] **AGENT-INFRA-001** — ten repository-local agent skills under `.claude/skills/`,
  `docs/AGENT_WORKFLOW.md`, a `CLAUDE.md` section. Routing dry-run by independent agents on three
  tasks; 24 defects found and fixed. No production behaviour changed.

* [x] **CI-PERF-001** — CI 276.3 → **100.6 min (2.75×, −63.6 %)**

  * causes: `build-test` ran tests **serially**; sharding balanced count, not runtime
  * `build-test` now 7 runtime-balanced shards with `-j$(nproc)`; sanitizers rebalanced
  * 1977 listed / 1932 executed / 45 disabled — **unchanged**
  * both coverage audits PASS; audit detects 6/6 injected defects
  * qualified on run 35385979133, 17/17, exact SHA `548401a`

Evidence:

```text
results/cuda-qual-001/    results/agent-infra-001/
results/gpu-pcorr-001/    results/ci-perf-001/
```

GitHub runners have **no GPU**: CI does not validate `sm_89` execution.
GPU evidence is local only — `results/cuda-qual-001/`, `results/gpu-pcorr-001/`.

---

# 2. Processing

**None.**

---

# 3. Next

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
AGENT-INFRA-001          ✓
   ↓
CI-PERF-001              ✓  100.6 min, 2.75×, run 35385979133
   ↓
Known-good baseline      ✓  548401a
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
