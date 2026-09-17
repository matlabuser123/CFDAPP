# P12-DIFF-002-W8-INV-001 — Historical W8 Gate Investigation (plan and execution record)

**Investigation only.** No production numerics, no W8 test, no threshold, no reference, no
extraction and no mesh family is modified. W8's failed result is preserved verbatim
(`results/p12-diff-002/w8/logs/01_W8.log`, hashed in `logs/00_freeze.log`).

## Subject — three assertions, classified separately

| # | assertion | source |
|---|---|---|
| 1 | StructuredQuad **velocity** order ≥ 1.5 on each pair | `test_structured_quad_production_case.cpp:406` |
| 2 | StructuredQuad **dp/dx** order ≥ 1.5 on each pair | `:407` |
| 3 | MultiBlock **G** order ≥ 1.5 on each pair | `test_multiblock_production_case.cpp:502` |

The first failed criterion in W8 is **#1** (measured 1.4390906187401413).

## Frozen baseline (step 1)

`logs/00_freeze.log`: both W8 test sources, mesh generators, extraction/validation utilities,
the relevant production numerical files, `libcfdcore.a`
(`143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a`), the original W8 gate, all
DIFF-002 lineage gates, VAL-001's gate, MESH-001's evidence and MESH-003's gate + evidence, and
the preserved W8 failure.

## Provenance (step 2) — the two cases do NOT share a cause

**StructuredQuad.** The test's own comment records the threshold as **empirically frozen**:

> "Observed order of each successive grid pair (r = 1.5): the formal order is 2; **bound 1.5
> (measured velocity 2.12 / 2.01, dp/dx 1.56 / 1.70)**."

`results/p12-mesh-001/summary.md` adds the decisive detail:

> "dp/dx: pair orders **1.561, 1.697**; triplet `p = 1.416`, **monotonic_not_asymptotic**;
> Richardson −1.2019386 (0.16 % from exact)… Gates: every pair order `>= 1.5` (formal 2)."

So `1.5` was locked **0.061 below the then-measured minimum**, on a sequence the convergence
framework *itself* labelled non-asymptotic. It was never analytically derived.

**MultiBlock.** `results/p12-mesh-003/acceptance_gate.md` G4(b):

> "The observed order of `velocity_l2_error` and of the G error is ≥ 1.5 on both grid pairs.
> *Rationale:* formal order 2; **≥ 1.5 is the P12-MESH-001 grid-study criterion**."

i.e. **inherited** from MESH-001's empirical choice, not independently derived. The same gate
already concedes one extraction is grid-dependent: "the radii move with the grid, so the
grid-convergence analysis (G4 c) therefore uses the ratio `radial_pressure_rise / exact`".

## Tooling

| file | role | fidelity check |
|---|---|---|
| `tools/w8inv_structured.cpp` | extended refinement of the distorted Poiseuille case; mesh quality, extraction window and solver status recorded; `tol` mode runs the tolerance-contamination diagnostic | reproduces the test's 64×8 and 96×12 and 144×18 values exactly |
| `tools/w8inv_multiblock.cpp` | extended refinement of the curved channel, through `CaseReader` + `CaseBuilder` so interfaces, patches and BCs are the production ones | reproduces the test's three grids and its 2.054 / 1.050 / 5.059 orders exactly |
| `tools/freeze.sh`, `tools/run.sh` | baseline freeze; build-and-run harness (`LIB` selects a library) | — |

Both probes copy the mapping, constants, solver settings and extraction **verbatim** from the
frozen test sources; the reproduction checks above are what licenses using them for the extended
grids.

## Refinement families

| case | the test's family | extension |
|---|---|---|
| StructuredQuad | 64×8, 96×12, 144×18 (r = 1.5) | + 216×27 (r = 1.5); and an independent r = 2 family 64×8, 128×16, 256×32 |
| MultiBlock | 8×20×3, 12×30×3, 18×45×3 (r = 1.5) | **cannot extend at r = 1.5** — the next `nt` would be 67.5, not an integer; so an independent r = 2 family 8×20, 16×40, 32×80 |

A second refinement ratio is run because an observed order that is real must not depend on the
ratio.

## Steps

| step | requirement | artifact |
|---|---|---|
| 1 | freeze | `logs/00_freeze.log` |
| 2 | provenance of each assertion | above; `summary.md` |
| 3 | StructuredQuad velocity, extended | `logs/02`, `logs/05` |
| 4 | StructuredQuad dp/dx, zero-crossing analysis | `logs/02`, `summary.md` |
| 5 | MultiBlock G (and velocity, rise, separately) | `logs/04` |
| 6 | mesh-family validity under refinement | `logs/02`, `logs/04` |
| 7 | extraction validity | `logs/02`, `logs/04` |
| 8 | pre- vs post-DIFF-002 on the same extended grids | `logs/06` |
| 9 | independent references | `summary.md` |
| 10 | non-vacuity of any proposed replacement | `logs/07` |
| 11 | classify each assertion separately | `summary.md` |
| 12 | decision | `summary.md` |

## Restrictions observed

No production change; no W8 test change; no threshold, reference, extraction or mesh-family
change; MESH-001/MESH-003 gates untouched; W9/W10 not run; GRAD-002 not revisited; MESH-007 not
rerun; MESH-004 ASan untouched; no commit; no push. Every failed result preserved.
