# P12-GRAD-002-DRIFT-001 — fix gate: the two W8 validation cases select the Rhie–Chow flux

Frozen before the committed cases are edited. Its hash is recorded in
`results/p12-diff-002/w8b/logs/00_freeze.log`, together with the W8B amendment it enables. The
evidence and derivation are in [summary.md](summary.md).

## 1. Defect and fix

**Defect** (summary.md §1). The 2D default face flux (`Automatic` → `Linear`) leaves an undamped
odd-even pressure mode on open domains. The SIMPLE fixed point is reached only after tens of
thousands of iterations and carries a large spurious pressure oscillation. Solutions stopped at the
committed tolerances depend on the stopping point.

- `cases/poiseuille_distorted`: velocity L2 moves +7 %, +16 % and +42 % on the three W8 grids.
- `cases/curved_channel_multiblock`: the G error moves by 507 %, 364 % and 140 %.

Both cases are the references of frozen grid-convergence validation, so the defect makes that
validation meaningless.

**Fix** (smallest, configuration only). Add `"face_flux": "rhie_chow"` to the `solver.json` of
those two cases, and extend each `case.json` description by one sentence stating why.

- Precedent: the committed 3D cases select it the same way.
- No production source changes.
- The 2D default does not change. That is a separate scope decision, recorded as open.

## 2. Criteria

Executed after the edit, in order. The first failure stops the gate.

| id | criterion | threshold / origin | pre-freeze dry-run |
|---|---|---|---|
| **F1** | **Scope.** The only repository changes are the two `solver.json` files (the `face_flux` key, nothing else) and one appended sentence in each of the two `case.json` descriptions. Every other file under `cases/`, and every production source under `src/` and `include/`, stays byte-identical. | exact | — |
| **F2** | **Stationarity (the reproducer).** For each case at the W8 test's finest grid, a re-solve with every outer tolerance ÷100 must **converge**, and it must move each W8 order quantity by ≤ 10 % of that quantity's own error. The quantities are SQ 144×18 velocity L2, and MB 18×45 velocity L2 and G. | ≤ 10 %: W8-INV-001 §13 R4, the iterative precondition already authorized for W8A. Implemented as W8B-4. | **linear flux (before): FAILS**, both tests, because the ÷100 solve does not converge (`dry_linear`). Probe: G moves 140 %. **Rhie–Chow: 0.043 % / 0.68 % / 0.41 %** (`dry_cand`). |
| **F3** | **The mode is gone.** The streamwise (SQ) and θ (MB) odd-even pressure projection at the committed grids is ≤ 1e-4 of the pressure-fit scale, and the state is bit-stationary between 5000 and 20000 iterations. | probe (drift_probe r1/r3) | Rhie–Chow: SQ 64×8 \|cb\| 1.2e-5, MB 8×20 1.3e-6, **bit-identical 5k vs 20k**. Linear: 0.12 → 0.72 and 0.018 → 0.24. |
| **F4** | **Accuracy and conservation unchanged in kind.** `StructuredQuadProductionCase.*` and `MultiBlockProductionCase.*`, with W8B's tests, pass from a fresh authoritative build. This covers each case's frozen per-grid, conservation, interface, transparency and export assertions. | the suites' own frozen thresholds | **15/15 pass** (`dry_cand`) |
| **F5** | **Non-vacuity.** Without the fix, the W8 tests reject the case (F2). With the fix, the corrupted-operator and two-point controls are still rejected. | see W8B §6 | `dry_linear`, `dry_farx2`, `dry_signflip`, `dry_nodiff` |

## 3. Explicitly not claimed

- This does **not** fix the linear flux for other 2D open-domain cases. The general defect is
  recorded in TODO → Known Technical Debt, and making Rhie–Chow the 2D default needs a user scope
  decision.
- The pre-existing fine-grid instability with one non-orthogonal pass (summary.md §4) is unchanged.
  The committed grids converge.
- The MESH-003 case generator script is **not** edited. It is evidence of that phase. The committed
  `solver.json` now deliberately differs from its output by the `face_flux` line, and this is
  documented here.
