# P12-DIFF-002-UC-001 — Poiseuille Discrete-Exact Reference Derivation (plan and execution record)

Investigation directory: `results/p12-diff-002/uc-001/`. Authorized as Part A of two sequential
investigations; Part B (`UF-001`) starts only if Part A completes.

**Scope rule observed throughout: no production source file is modified.** UC-001 changes only
validation-test reference formulas, and only after `acceptance_gate.md` was frozen.

## Frozen inputs

Library under test (unchanged by UC-001):

```
build/release/src/libcfdcore.a  143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a
```

Pre-modification test-file hashes: see `acceptance_gate.md` §6.

Historical gates, all verified intact before and after UC-001 (none rewritten, none deleted):

```
51079f6d…  acceptance_gate.md                          c4b08824…  acceptance_gate_A1.md
3a268060…  acceptance_gate_A2.md                       f6dbdd06…  acceptance_gate_A3.md
833ad560…  validation-migration/acceptance_gate.md     9944d066…  thermal-interface-fix/…
fbc927a2…  validation-migration/acceptance_gate_A5.md  1e2c9868…  rob-001/acceptance_gate.md
ba33ced0…  validation-migration/acceptance_gate_A6.md
bed6b894…  uc-001/acceptance_gate.md  (frozen by this investigation)
```

## Steps, as authorized and as executed

| step | requirement | artifact | outcome |
|---|---|---|---|
| 1 | freeze the investigation directory and hashes | this file, `acceptance_gate.md` §6 | done |
| 2 | reconstruct the historical 16/11 reference independently from the old two-point treatment; show the complete discrete equations; do not rely on comments or current test constants | `tools/uc001_reference.py`, `logs/01_reference.log` | reproduced `u_j = -(G/2)y_j(H-y_j) + Gh²/8`, `G/G_exact = ny²/(ny²+2)`, centreline `16/11`, drop ratio `64/66` — the exact constants the tests asserted |
| 3 | derive the DIFF-002 discrete system analytically, starting from the stencil coefficients, for arbitrary `ny` | `acceptance_gate.md` §1 | `cP=3Γ\|S\|/h`, `cF=Γ\|S\|/(3h)`, `cB=8Γ\|S\|/(3h)`, `cP-cF=cB` exactly; wall row `-4u₀+(4/3)u₁=Gh²`; `G/G_exact = 2ny²/(2ny²+1)` |
| 4 | implement an investigation-only reference solver outside CFDApp, exact/rational where practical; do not import or link CFDApp | `tools/uc001_reference.py` | exact `Fraction` Gauss-Jordan on the `ny+1` system (rows + flow-rate closure); zero CFDApp imports |
| 5 | compare with CFDApp to a pre-registered tight tolerance — **every cell**, plus matrix structure, RHS, centreline, dp/dx, wall contribution | `tools/uc001_crosscheck.cpp`, `logs/02_crosscheck.log` | 5 levels, worst deviation **2.2e-16**; `cB` re-tested with a nonzero wall value because the `u_b = 0` RHS check is vacuous; global momentum balance closes to 9.3e-16 → **no disagreement** |
| 6 | derive the continuous-solution error over several resolutions; verify or refute the "≈1.30x centreline / ≈3.5x dp/dx" observation; establish convergence order; improvement is not an acceptance criterion | `logs/01`, `logs/04`, `logs/05` | both operators order **2.000**; centreline ratio 1.303→4/3 **confirmed**; dp/dx ratio **3.909→4.000, correcting A6's 3.5** (which came from production's biased measured dp/dx) |
| 7 | non-vacuity: the reference must distinguish old two-point, correct DIFF-002 and a deliberately corrupted coefficient/sign; all wrong controls must fail | `logs/01`, `logs/07_gate_dryrun.log` | four controls (two-point, far-cell ×2, far-cell sign flip, far-cell dropped) fail every criterion C1–C6; weakest margin 4.6x |
| 8 | classify the three U-C tests | `acceptance_gate.md` §5 | `Profile` and `PressureDrop` → **MIGRATE_DERIVED_REFERENCE**; `ProductionGridConvergence` → **MIGRATE_DERIVED_REFERENCE + REDESIGN_VALIDATION** (one gate); `MassFlow` → KEEP. No `PRODUCTION_DEFECT`, no `UNCERTAIN` |
| 9 | freeze the gate with SHA256 **before** modifying authoritative tests; prefer a formula generated from mesh parameters over copied production output | `acceptance_gate.md` = `bed6b894…` | frozen first, then migrated; every reference side is a function of `ny` |
| 10 | fresh rebuild and verification; all three U-C failures resolved with no new failures | `logs/08`, `logs/09`, `logs/10` | see `summary.md` |

## Tooling

| file | role | CFDApp? |
|---|---|---|
| `tools/uc001_reference.py` | exact-rational independent reference + wrong controls | no |
| `tools/uc001_parity.py` | centreline sampling-parity derivation; exact gate constants | no |
| `tools/uc001_gridorder.py` | attribution of the dp/dx order overshoot | no (production numbers labelled) |
| `tools/uc001_gate_dryrun.py` | pre-registration dry run of every criterion | no (production numbers labelled) |
| `tools/uc001_crosscheck.cpp` | 5-level machine-precision comparison against production | links `libcfdcore.a` |
| `tools/uc001_grids.cpp` | production grid sweep, ny 8/12/18/27 | links `libcfdcore.a` |
| `tools/run.sh`, `run_grids.sh`, `run_uc_tests.sh`, `run_step10.sh` | build/run harnesses | — |
