# P12-GRAD-002-DRIFT-001 — the "iterative drift" root cause

Investigation (2026-09-17). It was authorized as part of the DIFF-002 → GRAD-002 → MESH-007
correctness chain, because the drift makes W8's accuracy criteria depend on where the solver stops
(`results/p12-diff-002/w8a/summary.md`).

- No production source was modified.
- Probe: [tools/drift_probe.cpp](tools/drift_probe.cpp). It uses the production case path
  (CaseReader → CaseBuilder → SIMPLE with each case's own `solver.json`, reference cell 0), changes
  one setting at a time, and records:
  - odd-even projections of the pressure-fit residual: `cb(i+j)`, `cb(i)` (streamwise), `cb(j)`;
  - residual histories;
  - solution checkpoints, from independent runs of N iterations out of the same initial state.
- Libraries used:
  - the authoritative tree (`143a1dda…`);
  - the tree without DIFF-002 (`719d0fc7…`);
  - the pre-GRAD-002 tree (`eaadaa63…`);
  - UF-001's far-cell ×2 control (`078668ba…`) and sign-flip control (`1e5fbb7b…`).

## 1. Root cause — the 2D linear face flux has an undamped odd-even pressure mode on open domains

`FaceFluxScheme::Automatic` resolves to `Linear` in 2D (`SIMPLESettings.hpp`). That flux carries no
pressure–velocity stabilisation, a limitation already documented in the header and in ROADMAP. On
an open domain (inlet / outlet), the odd-even pressure mode is not damped. The SIMPLE iteration
then converges to its fixed point **very slowly**, and the fixed point carries a large spurious
odd-even pressure component.

| evidence | log |
|---|---|
| **StructuredQuad 64×8, committed settings.** Residuals fall only ~12 % per 1000 iterations. The streamwise odd-even amplitude `cb(i)` grows 0.12 → 0.30 → 0.52 → 0.72 → 0.80 at the gate / 5k / 10k / 20k / 40k iterations, and velocity L2 moves 1.083e-2 → 1.181e-2. The state at 20000 is **not** a plateau: from 20000 to 40000 it keeps moving. | r1_sq64_repo_base_{hist,ckpt} |
| **MultiBlock 8×20.** The θ-direction mode `cb(j)` grows 0.018 → 0.32 by 40k iterations. The G error moves +8.3e-3 → −4.8e-2 (the mode contaminates the per-ring slope). Velocity barely moves. | r1_mb8_repo_base_ckpt |
| **Every linear-flux variant carries the mode:** N = 0, 1, 2; least squares; upwind; BiCGSTAB or CG for the pressure. | r1_*_repo_{n0,n2,ls,up,bicg}_ckpt |
| **Rhie–Chow removes it.** SQ 64×8 converges in 39 iterations, MB 8×20 in 44, with `cb` ≈ 1e-5 / 1e-6. Residuals then sit at the linear-solver floor (~1e-10) and stay **bit-identical** from ~1000 to 20000 iterations. | r1_*_repo_rc_{hist,ckpt} |
| **The defect predates GRAD-002 and DIFF-002.** The pre-GRAD-002 and no-DIFF-002 libraries show the same slow evolution (SQ 64×8 `cb(i)` 0.019 → 0.24 before GRAD-002). GRAD-002 makes the mode's effect on velocity larger: 40k-iteration velocity drift is +0.1 % before GRAD-002, +3 % without DIFF-002 and +9 % now. INV-001's "the baseline is stationary" measured only that small velocity effect. | r1_sq64_{pregrad,nodiff}_base_ckpt |
| **It is not specific to non-orthogonal meshes.** | r2_*_ckpt, r5b_sq0_*_base |
| &nbsp;&nbsp;• Cartesian `poiseuille_flow`: `cb(i)` 0.09 → 0.32, velocity moves up to 1.8e-3. | |
| &nbsp;&nbsp;• Graded `channel_transpiration_graded`: 0.24 → 0.56. | |
| &nbsp;&nbsp;• Orthogonal `structured_quad` control: the dp/dx error goes +8.1e-3 at the gate → −1.86e-2 at 20k, against the exact discrete +9.30e-3. | |
| &nbsp;&nbsp;• The **closed** lid-driven cavity converges properly: the mode is present but constant. | |
| &nbsp;&nbsp;• With Rhie–Chow, all of these converge in 29–115 iterations to bit-identical states. | |

**Classification: pre-existing production convergence defect of the 2D linear face flux on open
domains.** It is not a GRAD-002 defect: the continuous boundary gradient only couples the spurious
mode more strongly. W8-INV-001's "residual plateau" is this slow evolution. W8A's two
unreproducible criteria (W8A-1, W8A-4) are its symptoms.

## 2. With the mode removed, the discrete solution is the independently derived one

On the **orthogonal** `structured_quad` control (Rhie–Chow, N = 2, 4000 iterations), the dp/dx
error equals UC-001's exact rational discrete value, `1.2/(2 ny² + 1)`:

| ny | measured | UC-001 exact |
|---|---|---|
| 8 | 9.302325e-3 | 9.302326e-3 |
| 12 | 4.152249e-3 | 4.152249e-3 |
| 16 | 2.339178e-3 | 2.339181e-3 |
| 18 | 1.848996e-3 | 1.848998e-3 |
| 27 | 8.224812e-4 | 8.224812e-4 |
| 32 | 5.856455e-4 | 5.856515e-4 |

Velocity L2 equals UC-001's discrete-exact value too (8.4927e-3, 3.7905e-3, 1.6879e-3). So the
extraction is exact, and the stationary Rhie–Chow solution **is** the discrete solution. In fully
developed flow the Rhie–Chow correction vanishes for the linear pressure.

## 3. The W8 families, stationary

All with Rhie–Chow. The gate / 3000–4000-iteration states differ by ≤ 0.8 % of the error at the
test grids. Logs: r3_*, r5_sq_*, r5b_sq_*.

| StructuredQuad (distorted) | 64×8 | 96×12 | 144×18 | 216×27 | 128×16 | 256×32 | 512×64 (gate) |
|---|---|---|---|---|---|---|---|
| velocity L2 | 1.0921e-2 | 4.6448e-3 | 2.0060e-3 | 8.812e-4 | 2.5615e-3 | 6.259e-4 | 1.770e-4 |
| ÷ orthogonal discrete-exact L2 | 1.286 | 1.225 | 1.189 | 1.174 | 1.200 | 1.171 | — |
| dp/dx signed error | −3.735e-3 | +1.2545e-3 | +1.2899e-3 | +7.314e-4 | +1.4309e-3 | +5.514e-4 | +8.44e-5 |
| \|error\| ÷ orthogonal discrete-exact | 0.402 | 0.302 | 0.698 | 0.889 | 0.612 | 0.941 | — |

- **Velocity is asymptotically second order.** Pair orders are 2.108, 2.071, 2.029 at r = 1.5, and
  2.092, 2.033 at r = 2.
- **dp/dx is the sum of two terms:**
  - the orthogonal discrete-exact term, `+1.2/(2ny²+1)`, second order and positive;
  - a distortion term of opposite sign that dominates on the coarse grids.
  - The error therefore **crosses zero between ny = 8 and 12**, and pair orders are meaningless
    there (2.70 across the crossing, −0.05 on the next pair).
  - At the finest grids the ratio to the orthogonal term approaches 1, so the distortion term is of
    higher order.
- **The test's three grids are pre-asymptotic for dp/dx**, and an observed-order or GCI statement
  on them is invalid.

| MultiBlock (polar, self-similar) | 8×20 | 12×30 | 18×45 | 16×40 | 24×60 | 32×80 | 36×90 |
|---|---|---|---|---|---|---|---|
| velocity L2 | 1.1164e-2 | 4.8376e-3 | 2.0936e-3 | 2.6718e-3 | 1.1493e-3 | 6.277e-4 | 4.919e-4 |
| G signed error | +1.2345e-2 | +5.600e-3 | +2.4575e-3 | +3.1275e-3 | +1.3578e-3 | +7.429e-4 | +5.770e-4 |

- G orders are 1.95 and 2.03 (r = 1.5), 1.98 and 2.07 (r = 2), and 2.04 and 2.09 (r = 2 from
  12 and 18).
- Velocity orders are 2.06 and 2.07.
- **Clean, asymptotic second order.** The committed W8 MultiBlock assertions hold unchanged.

## 4. Separate, pre-existing — fine-grid instability with one correction pass

StructuredQuad 216×27 and 256×32 **fail with the committed N = 1** in every library (pre-GRAD-002,
no-DIFF-002, current) and with both fluxes (round 4). N = 0 fails fastest; the uncorrected recipe
also fails at 96×12 and 144×18 (round 6).

Rhie–Chow converges on these grids with N = 2 (227 / 303 iterations), or with relaxation 0.5/0.2 or
0.3/0.1. These settings reach **the same fixed point** (216×27 velocity L2 8.836e-4, 8.839e-4,
8.841e-4). This is the recorded MESH-004 debt ("the uncorrected recipe diverges on smooth
non-orthogonal meshes"). The committed grids converge with N = 1, so it does not block W8.

## 5. Controls, stationary (Rhie–Chow; round 6)

| library | SQ velocity L2 64/96/144 | SQ dp/dx err 64/96/144 | MB G err 8/12/18 | MB G orders |
|---|---|---|---|---|
| current | 1.092e-2 / 4.645e-3 / 2.007e-3 | −3.7e-3 / +1.25e-3 / +1.28e-3 | 1.23e-2 / 5.60e-3 / 2.46e-3 | 1.95 / 2.03 |
| two-point (no DIFF-002) | 1.757e-2 / 7.448e-3 / 3.281e-3 | +2.59e-2 / +1.41e-2 / +6.94e-3 | 5.89e-2 / 2.73e-2 / 1.24e-2 | 1.90 / 1.95 |
| far-cell ×2 | 5.340e-2 / 3.886e-2 / 2.782e-2 | +1.34e-1 / +1.03e-1 / +7.47e-2 | 2.13e-1 / 1.57e-1 / 1.12e-1 | 0.76 / 0.83 |
| sign flip (converges with Rhie–Chow) | 9.038e-2 / 6.482e-2 / 4.430e-2 | −2.69e-1 / −1.79e-1 / −1.19e-1 | −3.77e-1 / −2.62e-1 / −1.78e-1 | 0.89 / 0.96 |

## 6. Consequences, and the decision this raises

- **Committed W8 cases.** `poiseuille_distorted` and `curved_channel_multiblock` cannot give a
  unique converged answer with the linear flux. Their validation (grid convergence, iterative
  precondition) needs one. Selecting `"face_flux": "rhie_chow"` in their `solver.json` is the
  smallest change that makes them well-posed. There is precedent: the committed 3D cases already
  select it. A dry-run with the **unchanged** tests (logs/d1_dryrun_rc_cases.log) gives 13 / 15
  production-case tests passing, including the **original W8 MultiBlock test**. The two
  StructuredQuad failures are the pre-asymptotic dp/dx instruments of §3 and one empirical 1.5
  factor, now 1.495.
- **The general defect remains for every other 2D open-domain case** that uses the default flux.
  The complete fix is making Rhie–Chow the 2D default. That changes every 2D result and is listed in
  ROADMAP as unscoped; it is **a user decision, not taken here**.
- **Process lesson (recorded).**
  - Round 5's orthogonal-control logs are INVALID and are kept under `*.INVALID-*` names. The probe
    failed to compile (a heredoc turned a `\n` into a newline), and `run.sh` silently used the
    previous binary, which ran them as the MultiBlock case.
  - `run.sh` now keys the binary by source hash and fails closed.
  - Editing `run.sh` while jobs ran also truncated some round-5 logs' trailers; their result rows
    are intact.
