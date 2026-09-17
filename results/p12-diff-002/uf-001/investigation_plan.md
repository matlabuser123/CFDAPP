# P12-DIFF-002-UF-001 — Natural-Convection Benchmark Policy Investigation (plan)

Part B of the two sequential investigations. Part A (`UC-001`) completed
`P12-DIFF-002-UC-001 COMPLETE — U-C RESOLVED` and is **not reopened**: its gate
(`bed6b894…`), `plan.md`, `summary.md` and the three migrated Poiseuille files are hashed in
`logs/00_freeze.log` and re-verified at the end.

## Question

Two tests fail:

```
NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3
NaturalConvectionValidation.Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3
```

Both on the same assertion, `EXPECT_LT(r.vMaxError, 0.12)`, measured **0.1303**. Determine which
of A (production defect) / B (legitimate coarse-grid change, converging to the benchmark) /
C (the 10×10 criterion applies the benchmark in a numerically unjustified way) / D (sampling or
normalisation convention mismatch) / E (insufficient evidence) holds.

**The de Vahl Davis reference values are external and are never changed, redefined or replaced.**

## Frozen baseline (UF-001.1)

`logs/00_freeze.log` records sha256 for the natural-convection tests, the benchmark data and its
literature provenance copy, the momentum/thermal/buoyancy/boundary-diffusion/SIMPLE production
sources, `libcfdcore.a`, all ten frozen gates, and UC-001's deliverables.

Authoritative library: `143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a`.

Pre-DIFF-002 baseline: built in an **isolated copy outside the repo**
(`$HOME/uf001_baseline`, `tools/build_baseline.sh`) with exactly one block removed — the
DIFF-002 far-cell reconstruction in `boundaryFaceDiffusionTerms` — so the function falls through
to the pre-existing two-point treatment. `logs/03_baseline.log` shows the full diff and confirms
every other production numerical file is byte-identical. Library
`719d0fc7ae48c027756430a8473a9b47ef3f516167aa7eb7319fa3eb468c50fe`. On this cavity's orthogonal
Cartesian mesh the fall-through path reduces to exactly `Γ|S|/d`, i.e. the pre-DIFF-002 wall flux;
that equivalence is why the single-block removal is a faithful baseline **for this case**.

## Steps

| step | requirement | artifact |
|---|---|---|
| .1 | freeze the investigation baseline and the pre-DIFF-002 reference | `logs/00`, `logs/03` |
| .2 | establish the benchmark definition independently | `logs/02` notes, `data/wan_2001.txt` |
| .3 | verify nondimensional equivalence | `tools/uf001_equiv.cpp`, `logs/04` |
| .4 | reproduce both trajectories over a grid family | `tools/uf001_grids.cpp`, `logs/01,02,05,06` |
| .5 | separate field error from extrema-sampling error | same probe, five conventions |
| .6 | grid-convergence analysis | `tools/uf001_analysis.py`, `logs/09` |
| .7 | revisit the momentum-wall attribution | `tools/uf001_wallshear.cpp`, `logs/07,08` |
| .8 | continuum-limit behaviour | `logs/09` |
| .9 | classify each of the two U-F failures | `summary.md` |
| .10 | non-vacuity of any proposed replacement | `logs/10` |
| .11 | freeze the policy gate | `acceptance_gate.md` |
| .12 | amend only if justified | tests |
| .13 | focused verification | `logs/11` |
| .14 | fresh authoritative W7 | `logs/12` |
| .15 | decision after W7 | `summary.md` |

## Restrictions observed

No production change; no authoritative test change before the gate is frozen; de Vahl Davis
values untouched; no loosening of a literature tolerance to pass; UC-001, A6, W8, GRAD-002,
MESH-007 and the MESH-004 ASan defect untouched; no commit; no push. All historical failed gates
and amendments preserved.

## Tooling

| file | role | links CFDApp? |
|---|---|---|
| `tools/freeze.sh` | UF-001.1 baseline freeze | no |
| `tools/build_baseline.sh` | isolated pre-DIFF-002 library | builds it |
| `tools/uf001_equiv.cpp` | nondimensional-equivalence verification | yes |
| `tools/uf001_grids.cpp` | grid family, all quantities raw, five extraction conventions | yes |
| `tools/uf001_wallshear.cpp` | manufactured wall-shear order study, both operators | yes |
| `tools/uf001_analysis.py` | order / Richardson / GCI, parity-aware | no |
| `tools/run.sh` | build-and-run harness (`LIB` selects the library) | — |
