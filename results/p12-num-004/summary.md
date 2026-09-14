# P12-NUM-004 — Solver Robustness

**Status: COMPLETE — `[x]` in `TODO.md`** (2026-09-13). Every acceptance gate in §20 passes with the measured evidence below; §17 lists the remaining limitations, none of which is a failed gate.

Environment: WSL2 Ubuntu, GCC, `build/debug` (Debug), 2026-09-13. Every
number below was printed by a committed test (named alongside it), by
`ctest`, or by one of the scratch evidence drivers. The drivers link the
same library and are not committed: `exp.cpp` covers the extra cavity
cases in §8 and the tight-tolerance comparison in §10; `arkilic.cpp`
covers the Arkilic runs in §10; `baseline_dump.cpp` covers §13. Nothing is
estimated.

---

## 1. Starting baseline (before any NUM-004 change was built)

- Full regression on the post-NUM-003 tree: **1506/1506 passed**, 0
  failed, `ctest -N` = 1519 (13 disabled), 710.0 s. The build step
  reported `ninja: no work to do`, which shows the binaries predated every
  NUM-004 edit.
- Focused baseline: `CFDAlgebraTests` 67/67, `CFDSimpleTests` 99/99,
  `CFDCompressibleSimpleTests` 23/23.
- Bit-identity reference: the NUM-003 full-precision dump
  (`baseline_dump.cpp`, 2439 lines). It covers the SIMPLE cavity (GG and
  LS), the SIMPLE channel, PISO, the CompressibleSIMPLE cavity and channel,
  thermal, species and k-ε transport, and was produced from this exact
  pre-NUM-004 library.

### Audit — what existed (reused unchanged) and what did not

| Mechanism | Before NUM-004 |
|---|---|
| Linear-solver failure detection | `SolverStatus::{Breakdown, NonFiniteInput, NonFiniteResidual, InvalidSystem, MaxIterations}` in CG/BiCGSTAB — **reused unchanged** |
| Outer failure propagation | `SIMPLEStatus::{MomentumFailure, PressureCorrectionFailure, NonFiniteState, InvalidConfiguration}` and the CompressibleSIMPLE mirror — **reused unchanged** |
| Linear residual | `SolverResult::initialResidual` = ‖b − A x₀‖, warm-started for momentum, zero-start for p′. It is the outer residual definition, **unchanged** |
| Convergence decision (SIMPLE, CompressibleSIMPLE) | absolute only: u, v ≤ velocityTolerance; p ≤ pressureTolerance; continuity and global imbalance ≤ continuityTolerance; turbulence (SIMPLE). Kept as the default, with **identical comparisons** |
| Relaxation | fixed `velocityRelaxation`/`pressureRelaxation` for the whole run |
| Krylov methods | CG and BiCGSTAB (CPU and GPU). **No GMRES** |
| Preconditioners | None / Jacobi. `solver.json` exposes neither (case runs are unpreconditioned) |
| Normalization, stagnation, divergence trend, adaptive relaxation, fallback | **did not exist** |
| Pressure-correction matrix | symmetric when a FixedValue pressure patch exists (no reference pinning). The pinned closed-domain matrix is **not** symmetric (the pinned row is replaced by the identity) |

---

## 2. Architecture (one implementation of each piece)

| Component | File | Used by |
|---|---|---|
| `GMRES` (restarted, right-preconditioned) | `include/cfd/algebra/GMRES.hpp` | factory (`LinearSolverType::GMRES`), fallback policy |
| Fallback policy, `analyzeMatrix`, `FallbackLinearSolver`, `makeLinearSolverWithFallback` | `include/cfd/algebra/LinearSolverFallback.hpp` | SIMPLE, CompressibleSIMPLE (momentum, pressure, all passes) |
| `ResidualTracker`, `AdaptiveRelaxationController`, `OuterIterationMonitor`, diagnostics | `include/cfd/solver/SolverRobustness.hpp` | SIMPLE and CompressibleSIMPLE: one monitor per solve |
| Settings (`SolverRobustnessSettings`) | same header; held by `SIMPLESettings::robustness`, `CompressibleSIMPLESettings::robustness`, `io::SolverConfig::robustness` | parser, CaseBuilder, ProjectRunner, CaseWriter |
| Result diagnostics (`OuterIterationDiagnostics`) | same header; `SIMPLEResult::robustness`, `CompressibleSIMPLEResult::robustness` | JSONWriter, CLI |

Neither solver contains a private copy of any convergence, stagnation,
divergence or relaxation logic. Each calls `monitor.record(sample)` once
per completed outer iteration and reads `monitor.relaxation()` at the start
of the next one.

---

## 3. Normalized residuals

For each outer residual q ∈ {u, v, p, continuity}, with R_n the value
after completed iteration n:

```
reference_q = max { R_k : k <= K, R_k finite }        (K = normalization.referenceIterations, default 5)
floor_q     = the absolute tolerance of q             (velocity / pressure / continuity tolerance)
R_norm      = R_n / max(reference_q, floor_q)
```

- The reference is the maximum over the first K iterations. This is the
  scaled-residual convention of commercial codes (e.g. ANSYS Fluent's
  "largest of the first five iterations"). It is chosen over "iteration 1"
  because an equation can start at **exactly 0**. v of a cavity started
  from rest is the example: `SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline`
  checks v residual[1] = 0 and v normalized[1] = 0.
- **Zero-baseline policy.** A reference below the floor, including exactly
  0, is replaced by the floor. So:
  - there is never a division by zero, an infinity, or a "meaningless
    enormous" value;
  - R_norm then reads "multiples of the absolute tolerance";
  - a reference below the absolute tolerance carries no information the
    absolute gate lacks.
- Finite input always gives a finite output. A ratio that would overflow
  (1e300 / 1e-12) saturates at the largest finite double; this was found by
  `ResidualNormalization.NonFiniteInput` and fixed.
- Non-finite input is recorded as `latest` and flagged
  (`latestFinite()`/`nonFiniteSeen()`), but never enters the reference,
  best value or window statistics.
- The normalized histories are **always** reported, whatever the criterion
  (`SIMPLEResult::robustness.{u,v,pressure,continuity}NormalizedHistory`,
  plus the final values in the results JSON), one entry per completed
  iteration. They are verified finite in every SIMPLE test.

Tests: `ResidualNormalization.{NonzeroBaseline, ZeroBaseline, SmallBaseline, DecreasingIncreasingAndConstantSequences, NonFiniteInput}`
check exact values:

| case | reference | effective ref. | value | normalized |
|---|---|---|---|---|
| nonzero (0.5, 1.0, 0.8 → K=3) | 1.0 | 1.0 | 0.02 | 0.02 |
| zero baseline (0, 0, 0) | 0 | 1e-8 (floor) | 1e-6 | 100 (finite) |
| small baseline (1e-20, 2e-20) | 2e-20 | 1e-8 (floor) | 1e-9 | 0.1 (not 5e10) |
| NaN pushed | 0.5 (unchanged) | 0.5 | NaN | NaN (flagged) |

---

## 4. Convergence policy

`OuterIterationMonitor::record` decides convergence **first**.

- **Absolute (default):**
  `u <= velTol && v <= velTol && p <= presTol && cont <= contTol && global <= contTol && turbulence ok`.
  These are the same comparisons, operands and order as the
  pre-NUM-004 code in both solvers. `ConvergencePolicy.DefaultIsExactlyTheAbsoluteGate`
  pins the boundary: a residual exactly at the tolerance converges; one ulp
  above does not.
- **Normalized:** u, v and p each satisfy
  `R <= max(tol_n x reference, tol_abs)`. That is, normalized ≤ tol_n
  **or** already below the absolute tolerance. A tiny, arbitrary absolute
  scale therefore never blocks convergence, and a tiny or zero reference
  never makes convergence easier, because references are floored at the
  absolute tolerance. Continuity, global mass imbalance and turbulence
  **keep their absolute gates**, and every residual must be finite.

Measured (`SIMPLERobustnessTest.NormalizedResidualConvergence`, 8×8 cavity
at Re = 100):
- The absolute run (1e-8) takes **1728** iterations.
- The normalized run (1e-4) takes **754** iterations. Its references are
  u 0.05657, v 0.007178, p 0.06286, and its final normalized values are
  u 3.82e-6, v 3.02e-5, p 9.94e-5. Every gate holds at iteration 754, at
  least one fails at iteration 753, and global imbalance is ≤ 1e-8.

**No hidden convergence** (`ConvergencePolicy.NoHiddenConvergence`): with
normalized u, v, p = 1e-5 (below 1e-4):

| other state | verdict |
|---|---|
| continuity 1e-9, global 1e-9 | Converged |
| continuity **1e-3** (> 1e-6) | not converged |
| global imbalance **1e-3** | not converged |
| a NaN residual | not converged |
| v with reference 0: v = 5e-6 (> absolute 1e-6) | not converged; converges at v = 1e-6 |

**Scale invariance** (`ConvergencePolicy.NormalizationRemovesArbitraryScale`):
the same history scaled by 1e6 converges at the **same** iteration (181)
under the normalized criterion, and never under the absolute one.

---

## 5. Residual-history infrastructure (`ResidualTracker`)

This is one reusable class, used for u, v, p and continuity, plus a
convergence-distance tracker for stagnation. It holds:
- latest, reference, effective reference, normalized, best;
- the last W finite values in a fixed ring buffer (O(W) memory, O(1) push,
  O(W) window minimum, maximum and mean);
- `valueAgo(k)`;
- `bestBeforeWindow()`: the smallest value that has already left the
  window and whose index is ≥ a start iteration, maintained on eviction in
  O(1).

History storage is bounded: only the solver *results* keep per-iteration
histories, exactly like the pre-existing absolute histories. Tests:
`ResidualNormalization.*` (window statistics exact: min, max, mean,
valueAgo, bestBeforeWindow).

---

## 6. Stagnation detection

With the convergence distance `D_n = max over all gates of value/threshold`
(converged iff every gate ≤ 1), evaluated once
`n >= startIteration` and more than W values exist:

```
best_old    = min D over iterations [1, n - W]        (bestBeforeWindow)
best_new    = min D over the last W iterations        (windowMin)
improvement = (best_old - best_new) / best_old
Stagnated   <=> improvement < min_relative_improvement
```

- **Slow convergence is not stagnation.** A solve contracting D by ρ per
  iteration has improvement 1 − ρ^W. It is classified slow-but-converging
  while ρ < (1 − threshold)^(1/W), i.e. ρ < 0.99980 per iteration for the
  defaults (W = 50, 1%).
  `ResidualTrend.SlowConvergenceIsNotStagnation`: ρ = 0.999 (2.0% per
  20-window) runs on for 3000 iterations; ρ = 0.9999 (0.2%) is Stagnated
  at the first eligible iteration (30).
- **Startup transients:** there is no detection before `startIteration`
  (default 100).
- **Exact synthetic plateau** (`ResidualTrend.Stagnating`): 0.8^k decay for
  60 iterations, then a flat plateau. Stagnated at iteration **80** = 60 + W.

**Genuine case** (`SIMPLERobustnessTest.StagnationStatus`, 8×8 cavity,
tolerance 1e-16, which is below round-off):

| run | status | iterations | final u / v / p |
|---|---|---|---|
| detection off | MaxIterations | 6000 | 8.25e-13 / 7.84e-13 / 9.81e-13 |
| detection on (defaults) | **Stagnated** | **3151** | 8.25e-13 / 7.84e-13 / 9.81e-13 |

The residuals freeze at a round-off fixed point around iteration 3100.
They are bit-identical at 3151 and at 6000, so 2849 iterations were saved.
The status detail reads: "stagnation: the best convergence distance
improved by 0% over the last 50 iterations (9811 -> 9811; threshold 1%)".

Before that plateau the same run is a genuine **slow** convergence that is
**not** flagged. At iteration 3000 the pressure residual is still halving
every ~100 iterations while u and v have already hit round-off, and D keeps
improving about 29% per window.
`SIMPLERobustnessTest.NormalConvergenceIsNotStagnationOrDivergence`: the
default cavity with every detector on is bit-identical to the plain run
(Converged, 1032 iterations).

---

## 7. Divergence detection

Hard NaN/Inf detection is unchanged: the solvers still report
NonFiniteState and linear failures. The new finite-trend rules are
evaluated from `n >= startIteration + W`. For each q ∈ {u, v, p,
continuity}, with G = `growth_factor` and
B = max(floor_q, best value of q since `startIteration`, older than the window):

- **Persistent excursion:** every one of the last W values ≥ G·B.
- **Sustained runaway:** the last W values strictly increase and the
  newest is ≥ G × the oldest of them.
- **Overflowed norm:** a residual norm that became non-finite while the
  fields stayed finite. The windowed rules cannot see it, because trackers
  exclude non-finite values. This was found on the aggressive-relaxation
  case with the fallback on (residual norm → inf at finite fields) and
  added.

W consecutive iterations are always required, so a single spike never
triggers. `ResidualTrend.SingleSpikeDoesNotTriggerDivergence` uses a 1e6
spike and a burst of W − 1 values at 1000× the level; neither fires.

Divergence is checked before stagnation. A growth too slow to meet the
criterion before the stagnation window closes is reported as Stagnated (no
progress).

Exact synthetic checks:
- `ResidualTrend.Diverging`: ×1.5 per iteration after 20 decaying values →
  Diverging at iteration **30** (persistent rule).
- `ResidualTrend.SustainedRunawayFromAPlateau`: ×3 growth from a flat
  plateau → **44** (runaway rule).

**Genuine case** (`SIMPLERobustnessTest.DivergenceStatus`, 8×8 cavity at
Re = 100, fixed α = 0.9/0.9, a finite runaway):

| iteration | 1 | 5 | 8 | 10 | 20 | 50 | 90 |
|---|---|---|---|---|---|---|---|
| u residual (no detection) | 5.66e-2 | 2.06 | 2.58e2 | 4.81e3 | 1.48e11 | 3.23e30 | 5.43e58 → momentum BiCGSTAB breakdown (MomentumFailure) |

With detection on (defaults W = 10, G = 10, start 10), the solve stops at
iteration **20**, the earliest possible, as **Diverging** with finite
fields. The detail reads: "every one of the last 10 u residuals is >= 10 x
its best value since iteration 10 (4811)".

---

## 8. Adaptive under-relaxation

There is one controller (`AdaptiveRelaxationController`). It is updated
exactly once per **completed** outer iteration, and its factors are used
for the whole next iteration (never changed mid-iteration). The measure is
m_n = max(normalized u, v, p). The rules, in order:

| rule | action |
|---|---|
| first update | Hold |
| growth: m_n > 1.2 · m_(n−1) | **Decrease**: α ← max(min, 0.7·α); ceiling ← min(ceiling, max(min, 0.9·α_before)) |
| growing oscillation: the last 4 step ratios alternate strictly and m_n ≥ m_(n−2) | Decrease |
| 5 consecutive improvements | **Increase**: α ← min(max, ceiling, 1.05·α); a Hold if already at the max or ceiling |
| otherwise (flat or mild rise) | Hold |

- Velocity and pressure scale together, each clamped to its own bounds:
  **0 < min ≤ α ≤ max ≤ 1** always. This is validated, and the configured
  relaxation must lie within the bounds when the controller is enabled.
- **Ceiling memory.** Without it the controller "hunted" back into the
  unstable range after each recovery. Measured before the ceiling was
  added, fallback off:
  - 16×16, α = 0.9/0.9: 336 increases vs 48 decreases, and still not
    converged when a pre-existing pressure BiCGSTAB breakdown stopped it at
    iteration 1819;
  - 0.95/0.95: 352 vs 50, and MaxIterations at 2000.

  With the ceiling (and the fallback on), the 0.9/0.9 case converged in
  2500 iterations with 2 decreases.
- The constants (×1.05 at most every 5 iterations; ×0.7; 1.2 jump; 0.9
  ceiling) are **empirical**, chosen for gradual, bounded changes. They are
  documented in the header and exercised on the cases below.
- Disabled (default): always Hold, factors = the settings values.
  `AdaptiveRelaxation.DisabledPreservesFixedAlpha`, together with the
  bit-identity results in §15, covers this.

Controller unit tests (exact values): `ImprovingIncreasesGradually`
(0.5 → 0.525 at update 6 → 0.5·1.05⁴ at update 21), `GrowthReducesAlpha`
(+10% held, +36% → ×0.7), `PlateauHolds`, `GrowingOscillationReducesAlphaDecayingZigzagDoesNot`,
`RespectsBounds`, `RecoveryAfterGrowthStopsBelowTheUnstableFactor`
(recovers to 0.81 = 0.9 × 0.9, not 0.95), `Deterministic`.

**Genuine difficult case** (`SIMPLERobustnessTest.AdaptiveRelaxationRecovery`,
8×8 cavity at Re = 100, start α = 0.9/0.9, bounds [0.2, 0.95] × [0.05, 0.95]):

| run | status | iterations | final u / p |
|---|---|---|---|
| fixed 0.9/0.9 (divergence detection on) | **Diverging** | 20 | 1.48e11 / 1.34e6 |
| fixed 0.9/0.9, no detection | MomentumFailure (after growing to 5.4e58) | 90 | — |
| **adaptive from 0.9/0.9** | **Converged** | **672** | 2.35e-8 / 9.89e-7 |
| fixed 0.7/0.3 (conservative reference) | Converged | 1032 | 3.43e-8 / 9.95e-7 |

Relaxation history of the adaptive run (velocity = pressure here):

| iterations | 1–6 | 7 | 8–12 | 13 | 18 | 23 | 28 | 33 | 38–672 |
|---|---|---|---|---|---|---|---|---|---|
| α | 0.900 | 0.630 | 0.441 | 0.463 | 0.486 | 0.511 | 0.536 | 0.563 | **0.567** (ceiling = 0.9 × 0.63) |

Residual history of the adaptive run: u 5.66e-2 (1), 80.2 (7, peak of the
runaway), 4.63 (12), 6.78e-3 (50), 2.32e-4 (100), 1.59e-6 (400), 3.30e-8
(650). The fixed run's history is in §7. The controller reversed the
runaway at iteration 8, and from the aggressive start it converged
**faster than the conservative fixed setting** (672 vs 1032 iterations).

Further measured cases (scratch driver `exp.cpp`, same Re = 100 cavity,
tolerance 1e-6, fallback on where noted):

| case | fixed | adaptive |
|---|---|---|
| 8×8, 0.95/0.95 | diverges (residual → inf) | Converged, 985 it (3 decreases) |
| 12×12, 0.9/0.9 | diverges, fails at 190 | Converged, 1287 it |
| 16×16, 0.9/0.9 (fallback on) | diverges to 3e70, fails at 94 | Converged, 2500 it (2 decreases) |
| 16×16, 0.95/0.95 (fallback on) | diverges, fails at 89 | Converged, 2324 it |
| healthy 8×8, 0.7/0.3 | Converged, 1032 it | Converged, 953 it |
| healthy 16×16, 0.7/0.3 (fallback on) | Converged, 2412 it | Converged, 2414 it |
| 32×32, Re = 1000, 0.9/0.5 (fallback on) | diverges, fails at 132 | not converged: pressure fallback GMRES reached its 1000-iteration budget at 569, reported as PressureCorrectionFailure (§17) |

---

## 9. Linear-solver fallback

### GMRES (new, `GMRES.hpp`)

GMRES(m) is restarted, right-preconditioned, with modified Gram-Schmidt and
Givens rotations (Saad, Alg. 9.5), CPU only, m = `gmresRestart` (default
30, a textbook value, empirical).
- It minimizes the **unpreconditioned** residual, so its stopping test is
  exactly that of CG/BiCGSTAB.
- A lucky breakdown ends the cycle. A zero pivot is reported as
  `Breakdown`, never NaN.
- Tests `GMRESTest.*` (10): an SPD system agrees with CG to 1e-9; a
  non-symmetric system; restart 3 on n = 30 (deterministic); Jacobi;
  zero RHS; NaN input; iteration budget; a singular inconsistent system →
  Breakdown; restart 0 rejected; a GPU request falls back to CPU.

### Policy (`LinearSolverFallback.hpp`, the one authoritative policy)

- **Eligible primary statuses:** `Breakdown` and `NonFiniteResidual`
  (algorithmic failures). **Not eligible:** `NonFiniteInput`,
  `InvalidSystem` (bad data or configuration) and `MaxIterations` (a budget
  outcome, where retrying would multiply cost).
  `LinearFallbackTest.OnlyAlgorithmicFailuresAreEligible` covers this.
- **Candidates, respecting matrix properties:**

  | primary | candidates (in order) |
  |---|---|
  | BiCGSTAB | CG *only if proven SPD*, then GMRES |
  | CG | GMRES, BiCGSTAB |
  | GMRES | CG *only if proven SPD*, then BiCGSTAB |

- **Matrix-property restriction (`analyzeMatrix`, run only when a fallback
  is needed).** A matrix is CG-eligible iff it is **symmetric** (to
  1e-12 relative), has a **positive diagonal**, is **weakly diagonally
  dominant** in every row, has **at least one strictly dominant row**, and
  is **irreducible** (connected graph). Such a matrix is nonsingular
  (Taussky) and positive semi-definite (Gershgorin), hence SPD. This is a
  sufficient condition; an SPD matrix failing it is conservatively not
  given to CG. The 1e-12 tolerances absorb only summation-order rounding.
  - The COMP-002 channel's pressure matrix (FixedValue outlet, no pinning)
    is proven SPD → CG.
  - A closed-domain matrix with a pinned reference row is not symmetric →
    GMRES.
  - `LinearFallbackTest.DoesNotUseCGForUnsupportedMatrix` checks symmetric
    indefinite and non-symmetric diagonally dominant matrices: no CG
    candidate for any primary, and no CG attempt even with 3 attempts.
    `SpdMatrixIsCGEligibleAndPreferred` and
    `SingularOrReducibleMatricesAreNotCGEligible` cover the other side.
- **Each attempt:**
  - uses the **same preconditioner type** as the primary. This is valid
    for every method: Jacobi is SPD whenever CG is eligible, and a
    zero/near-zero diagonal gives `InvalidSystem`, recorded, never a
    silent drop. `FallbackKeepsAndRecordsThePreconditioner` checks it;
  - runs on the CPU;
  - starts from the latest finite iterate;
  - uses the primary's `maxIterations`;
  - converges to **exactly the primary's target**,
    ‖r‖ ≤ max(absTol, relTol·‖r₀,primary‖), so a recovered solve meets the
    accuracy the primary was asked for.
- **Limits:** `max_attempts` is in 0..3 (validated). The first converged
  attempt ends the sequence and there is no retry loop
  (`MaxAttemptsRespected`: exactly min(maxAttempts, candidates) attempts
  for 0, 1, 2, 3).
- **Failure propagates:** if every attempt fails, the last attempt's
  status is returned and the solver's existing
  MomentumFailure/PressureCorrectionFailure applies
  (`FallbackFailurePropagates`).
- **Healthy solves are untouched.** A primary success through the wrapper
  is bit-identical (`HealthySolveIsUntouched`). Disabled, or
  `max_attempts` = 0, returns the plain `makeLinearSolver` solver
  (`DisabledPropagatesFailure`).

### Diagnostics

- `SolverResult::fallback` records: primary type, status and iterations;
  CG eligibility; and each attempt's type, preconditioner, status,
  iterations and final residual. The combined result keeps the primary's
  `initialResidual`, which is the outer residual definition.
- `SIMPLEResult::robustness` records `linearSolverFallbacks`,
  `linearSolverFallbackRecoveries`, and the first 32 events (outer
  iteration, equation, full report).
- A failed solve's `statusDetail` reads, for example,
  "pressure-correction linear solve failed: primary BiCGSTAB Breakdown
  after 146 iterations; fallback GMRES MaxIterations after 1000
  iterations".
- The CLI prints fallback counts and the detail **only** when non-empty,
  so the output of a default healthy run is unchanged. The results JSON
  gets additive `robustness.*` keys.

### Real failure → fallback → success

The P12-COMP-002 failure mode is reproduced
(`SIMPLERobustnessTest.LinearSolverFallbackRecovery`), using the COMP-002
channel (48×8, inlet 20 m/s, fixed-pressure outlet) with its original
unpreconditioned BiCGSTAB pressure solver:

| | status | outer iterations | detail / events |
|---|---|---|---|
| fallback off | **PressureCorrectionFailure** | 32 completed | "BiCGSTAB Breakdown after 104 iterations" (the same point as the COMP-002 record: "Breakdown innerIters=104") |
| fallback on | continues to the budget (MaxIterations at 60) | 60 | 8 fallbacks, 8 recovered; first at outer iteration **33**, pressure-correction: BiCGSTAB Breakdown after 104 → matrix proven SPD → **CG Converged** (79 iterations, residual 9.0e-9) |

The residual history is identical up to iteration 32; the fallback changes
nothing before it is needed. Over 150 iterations (scratch driver) there
were 19 fallbacks, all recovered by CG.

CompressibleSIMPLE, the same channel from rest
(`CompressibleSIMPLERobustnessTest.FallbackRecovery`):
- fallback off: PressureCorrectionFailure after 63 iterations ("BiCGSTAB
  Breakdown after 199 iterations");
- fallback on: the solve continues to its budget (80), with 1 fallback at
  iteration 64, CG Converged, matrix proven SPD; density stays finite and
  positive.

---

## 10. CompressibleSIMPLE

CompressibleSIMPLE uses the same `OuterIterationMonitor` and the same
fallback policy. The EOS density iteration and pressure-density coupling
are untouched: relaxation only changes *which* α enters the unchanged
assembly.

| test | result |
|---|---|
| `RobustnessRegression`: default vs detectors + fallback on (healthy air cavity, R = 287.05, 300 K) | bit-identical (u, v, p, ρ, histories) |
| same, adaptive from 0.7/0.3 | **296 vs 440 iterations** to 1e-6 (24 increases, α → 0.95/0.95), ρ finite and > 0 |
| same, both driven to 1e-11 | fixed 1324 vs adaptive **913** iterations; max \|u_fixed − u_adaptive\| = **1.5e-9** (same solution) |
| at 1e-6 the two stopping points differ at 1.4e-4 | both are within 2.1e-4 (fixed) and 3.5e-4 (adaptive) of the 1e-11 solution — the residual tolerance, not the relaxation, sets that level (scratch `exp.cpp ctight`) |
| `LowMachRegression`: R = 1e10 gas vs SIMPLE, both with adaptive + normalized | max \|u_c − u_SIMPLE\| = **7.0e-8** (gate 1e-4); iterations 461 / 615. The iteration paths legitimately differ (CompressibleSIMPLE's pseudo-transient term), so the decisions coincide only for the first 8 iterations (decreases 0 / 2) |
| `FallbackRecovery` | §9 |

### Arkilic et al. (1997) production case, full ProjectRunner path

The case is `cases/compressible_channel_coupled` (warm-start SIMPLE then
CompressibleSIMPLE), scratch driver `arkilic.cpp`, with the same metric as
`CompressibleCoupledProductionCaseTest`:

| configuration | SIMPLE it | CompressibleSIMPLE it | fallbacks (recovered) | Arkilic L2 / L∞ | max mass-flow station deviation |
|---|---|---|---|---|---|
| A: as shipped (CG pressure, default robustness) | 12189 | 6827 | 0 | 1.2864% / 2.5517% | 4.7e-9 |
| B: + adaptive relaxation (defaults) | 11302 | 6620 | 0 | 1.2857% / 2.5505% | 6.3e-9 |
| C: **the original BiCGSTAB pressure config** + fallback | 12520 | 6855 | 1938 / 1938 and 102 / 102 | 1.2864% / 2.5519% | 2.9e-8 |

- A reproduces the P12-COMP-002 record (12189 iterations; 1.28% / 2.55%).
- B keeps the validation result and needs 7% / 3% fewer iterations; the
  adaptive factors ended at 0.567/0.323 and 0.804/0.345.
- C is the configuration that originally required a hand edit. It now
  completes automatically with the same validation result.

---

## 11. Status model and propagation

- `SIMPLEStatus` and `CompressibleSIMPLEStatus` gain `Stagnated` and
  `Diverging`, **appended** so existing values keep their numbers.
- They are produced only when the corresponding detector is enabled.
  `statusDetail` explains why.
- Mapping (every switch updated: ProjectRunner ×2, CLI, JSONWriter, the
  compressible status name):

| status | ProjectRunStatus | exit code |
|---|---|---|
| Stagnated | DidNotConverge (results written) | 3 |
| Diverging | NumericalFailure | 4 |

`ProjectRunnerTest.DivergenceDetectionReportsNumericalFailure` and
`StagnationDetectionReportsDidNotConverge` check this end to end; the
exported JSON contains "Stagnated" and the `robustness` object.
`JSONWriterTest.RobustnessStatusesAndDiagnosticsAreExported` covers the
writer.

---

## 12. Configuration (`solver.json`, optional `robustness` block)

The keys are `convergence_criterion` (`absolute` | `normalized`),
`normalization{reference_iterations, velocity_tolerance, pressure_tolerance}`,
`stagnation_detection{enabled, window, min_relative_improvement, start_iteration}`,
`divergence_detection{enabled, window, growth_factor, start_iteration}`,
`adaptive_relaxation{enabled, min_velocity, max_velocity, min_pressure, max_pressure}`
and `linear_solver_fallback{enabled, max_attempts}`. `GMRES` is also
accepted as a linear-solver `type`.

- **Defaults:** absent block, empty block or absent key → absolute
  criterion, every feature disabled (`RobustnessConfigTest.AbsentBlockDefaultsToEveryFeatureOff`,
  `PartialBlockKeepsDefaultsForAbsentKeys`).
- There is a single source of defaults: `io::SolverConfig` holds
  `cfd::solver::SolverRobustnessSettings` directly.
- **Rejected with the field named** (`InvalidValuesAreRejectedWithTheFieldNamed`,
  20 cases):
  - a window of 0, 1 or negative;
  - `growth_factor` ≤ 1;
  - a relaxation bound outside (0, 1];
  - min > max;
  - an initial relaxation outside the bounds when adaptive is enabled;
  - `max_attempts` −1 or 4;
  - a non-boolean `enabled`;
  - `reference_iterations` 0;
  - a normalized tolerance ∉ (0, 1);
  - an improvement ∉ (0, 1);
  - an unknown criterion or key;
  - a non-object block.
- The same constraints are enforced by `validateSolverRobustnessSettings`
  for programmatic callers (→ `InvalidConfiguration`).
- **Writer:** the block is written only when it differs from the defaults,
  so a default case's `solver.json` is unchanged; a non-default block
  round-trips exactly.
- `ProjectRunner` passes the same settings to the coupled CompressibleSIMPLE.
- Documented in `docs/user_guide/case_format.md` (the `robustness`
  section and the exit-code table).

---

## 13. Compatibility (defaults preserve behavior)

- **Library-level bit identity:** the NUM-003 dump program rebuilt against
  the NUM-004 library gives 2439 lines, **byte-identical** (`cmp`) to the
  pre-NUM-004 output. This covers SIMPLE cavity (GG, LS), SIMPLE channel,
  PISO, CompressibleSIMPLE cavity and channel, thermal, species and k-ε.
  Result: `BIT_IDENTICAL_TO_PRE_NUM004`, both right after the first build
  and again on the final library.
- **Test-level:**
  - `SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline`: default ≡
    explicit default ≡ (stagnation + divergence + fallback enabled on a
    healthy case), bit for bit; relaxation histories constant.
  - `NormalConvergenceIsNotStagnationOrDivergence`, and the compressible
    `RobustnessRegression`: bit-identical.
  - `LinearSolverFallbackRecovery`: identical history up to the failure
    point.
- The full regression (§19) passes with every pre-existing test unchanged.

## 14. Determinism

- `SIMPLERobustnessTest.Deterministic`:
  - the adaptive and stagnation and fallback cavity: identical status,
    residual, relaxation and normalized histories;
  - the COMP-002 channel with fallback: identical events (iteration,
    equation, attempt types, iterations).
- `AdaptiveRelaxation.Deterministic` and `LinearFallbackTest.Deterministic`
  cover the unit level.
- Every rule depends only on the sequence of residuals; there is no
  randomness, timing or threading.

## 15. Performance

- **Bookkeeping cost** (`SolverRobustnessPerformance.BookkeepingIsFixedWindowCost`,
  Debug): 100,000 outer-iteration updates with every detector evaluated
  every iteration (stagnation W = 50, divergence W = 5, adaptive on) took
  0.101 s, i.e. **1.0 µs per outer iteration**. It is O(W) per update with
  fixed memory: the u tracker capacity stays 5.
- **End to end** (`SIMPLERobustnessTest.BookkeepingOverheadIsSmall`, 8×8
  cavity, 1032 iterations, bit-identical results): robustness off 1.580 s;
  detectors + fallback wrapper on 1.557 s (ratio 0.985, within noise).
- The matrix analysis runs only after a primary failure.

---

## 16. Tests added

73 new tests in total.

| suite | tests |
|---|---|
| `GMRESTest` (10) | SolvesSPDSystemAndAgreesWithCG, SolvesNonSymmetricSystem, RestartedSolveConvergesDeterministically, JacobiPreconditionedSolveMatches, ZeroRightHandSideConvergesImmediately, NonFiniteInputIsRejectedBeforeIterating, IterationBudgetIsReportedAsMaxIterations, SingularInconsistentSystemReportsBreakdown, InvalidRestartLengthIsRejected, GpuRequestFallsBackToCpu |
| `LinearFallbackTest` (13) | PrimaryBiCGSTABGenuinelyBreaksDownOnIndefiniteSystem, **DisabledPropagatesFailure, EligibleBreakdownUsesFallback, SuccessRecorded, FallbackFailurePropagates, MaxAttemptsRespected, DoesNotUseCGForUnsupportedMatrix**, SpdMatrixIsCGEligibleAndPreferred, SingularOrReducibleMatricesAreNotCGEligible, FallbackKeepsAndRecordsThePreconditioner, OnlyAlgorithmicFailuresAreEligible, HealthySolveIsUntouched, Deterministic |
| `ResidualNormalization` (5) | **NonzeroBaseline, ZeroBaseline, SmallBaseline**, DecreasingIncreasingAndConstantSequences, **NonFiniteInput** |
| `ResidualTrend` (8) | **Converging, SlowConvergenceIsNotStagnation, Stagnating, Diverging**, SustainedRunawayFromAPlateau, **SingleSpikeDoesNotTriggerDivergence**, NonFiniteResidualIsDivergingWhenEnabled, DisabledDetectionNeverStops |
| `ConvergencePolicy` (4) | DefaultIsExactlyTheAbsoluteGate, NoHiddenConvergence, NormalizationRemovesArbitraryScale, NormalizedHistoriesAreReportedAndFinite |
| `AdaptiveRelaxation` (8) | **ImprovingIncreasesGradually, GrowthReducesAlpha**, PlateauHolds, GrowingOscillationReducesAlphaDecayingZigzagDoesNot, **RespectsBounds**, RecoveryAfterGrowthStopsBelowTheUnstableFactor, **DisabledPreservesFixedAlpha, Deterministic** |
| `SolverRobustnessSettingsTest` (2), `SolverRobustnessPerformance` (1) | DefaultsAreValidAndDisabled, InvalidValuesAreRejected; BookkeepingIsFixedWindowCost |
| `SIMPLERobustnessTest` (10) | **DefaultRobustnessPreservesBaseline, NormalizedResidualConvergence, StagnationStatus**, NormalConvergenceIsNotStagnationOrDivergence, **DivergenceStatus, AdaptiveRelaxationRecovery, LinearSolverFallbackRecovery**, Deterministic, InvalidRobustnessSettingsAreInvalidConfiguration, BookkeepingOverheadIsSmall |
| `CompressibleSIMPLERobustnessTest` (3) | **RobustnessRegression, FallbackRecovery, LowMachRegression** |
| `RobustnessConfigTest` (6) | AbsentBlockDefaultsToEveryFeatureOff, FullBlockParsesAndReachesSolverSettings, PartialBlockKeepsDefaultsForAbsentKeys, InvalidValuesAreRejectedWithTheFieldNamed, NonDefaultBlockRoundTripsAndDefaultIsNotWritten, GmresLinearSolverTypeIsAccepted |
| `JSONWriterTest` (1), `ProjectRunnerTest` (2) | RobustnessStatusesAndDiagnosticsAreExported; DivergenceDetectionReportsNumericalFailure, StagnationDetectionReportsDidNotConverge |

Bold marks the names required by the task, which map to the suite
prefixes above.

### Test fixes during development (all in new tests, recorded honestly)

- `ResidualNormalization.NonFiniteInput` found a real defect: a
  normalization overflow to inf. The fix is in the library (saturation).
- `SIMPLERobustnessTest.BookkeepingOverheadIsSmall` first used a 16×16
  cavity at 1e-12. That run genuinely hits the pre-existing BiCGSTAB
  pressure breakdown at iteration 132, so with vs without the fallback
  differ and the timing comparison was not like for like. It was moved to
  the breakdown-free 8×8 case.
- `CompressibleSIMPLERobustnessTest.RobustnessRegression` originally
  asserted a guessed 1e-4 agreement between two stopping points
  (measured 1.3e-4). It was replaced by the principled check: both
  strategies driven to 1e-11 agree to 1.5e-9.
- `CompressibleSIMPLERobustnessTest.LowMachRegression` originally required
  identical relaxation decisions. The premise was false, because
  CompressibleSIMPLE's pseudo-transient term changes the iteration path.
  It now keeps the low-Mach solution gate (1e-4; measured 7.0e-8) and
  reports the decisions.

### Focused suites (`focused_tests.log`): **232/232**

| binary / filter | passed |
|---|---|
| `CFDAlgebraTests` (all) | 90/90 |
| `CFDSolverTests` robustness suites | 28/28 |
| `CFDSimpleTests` SIMPLERobustness + Settings/Convergence/Failure/NonFinite/Determinism | 34/34 |
| `CFDCompressibleSimpleTests` (all) | 26/26 |
| `CFDIoTests` RobustnessConfig/JSONWriter/CaseReader/CaseWriter | 48/48 |
| `CFDAppLayerTests` ProjectRunnerTest | 6/6 |

---

## 17. Remaining limitations

1. **BiCGSTAB's absolute breakdown thresholds are unchanged**
   (`constants::tiny` tests on ρ, r̂·v, t·t and ω). Breakdowns near
   convergence are frequent on these pressure systems: 8 in 60 iterations
   of the COMP-002 channel; 7 in the healthy 16×16 cavity at 1e-6; and the
   16×16 cavity at 1e-12 fails at 132 without the fallback. Making the
   thresholds scale-aware would change the default behavior of every run
   that currently breaks down, so it was not done. The fallback is the
   opt-in remedy. Which threshold fires in each case was not instrumented.
2. **For non-SPD matrices the fallback has one method (GMRES(30),
   unpreconditioned for case runs).** It can fail. This was observed on the
   32×32 Re = 1000 cavity with adaptive relaxation: GMRES reached its
   1000-iteration budget on the pinned pressure matrix at iteration 569,
   and the failure propagated as PressureCorrectionFailure. There is no
   ILU/AMG preconditioner in the codebase.
3. The pinned closed-domain pressure matrix is not symmetric, so it is
   never CG-eligible (conservative, though its reduced system is SPD).
4. **The fallback can prolong a diverging run.** With fixed α = 0.9/0.9
   and the fallback on, GMRES kept rescuing momentum solves at 1e60 scale
   for 4000 iterations while the residual norms overflowed. Enable
   divergence detection together with the fallback; the non-finite-norm
   rule was added for exactly this.
5. The adaptive-controller constants are empirical, exercised on the
   cavity, air-cavity and Arkilic cases. Its ceilings never relax upward
   (a transient early growth caps α for the rest of the run).
6. The normalized references depend on the start: a restart from a
   converged state gives references at the floor, i.e. the absolute
   criterion.
7. A finite growth too slow for the divergence criterion is reported as
   Stagnated. The persistent-excursion rule could, in principle, fire on a
   legitimate transient that stays ≥ G × its best value for W iterations
   after `start_iteration`; detection is opt-in with conservative defaults.
8. Scope:
   - integrated into SIMPLE and CompressibleSIMPLE only. The thermal,
     species and turbulence Picard loops, PISO and the transient solvers
     keep their own convergence logic; the infrastructure is reusable, not
     yet wired there;
   - SIMPLE's turbulence residual keeps its absolute gate (it is included
     in the convergence distance);
   - no GUI controls: the GUI round-trips the block via CaseWriter;
   - no GPU GMRES (CPU fallback, logged).

---

## 18. Git state

See the final report. Nothing is committed, and the NUM-001..004 work is
all in the working tree.

## 19. Full regression

- **Command:** `ctest -j32 --output-on-failure` (WSL2, `build/debug`) on
  the final code.
- **Result: 1579/1579 passed, 0 failed.** `ctest -N` = 1592; the other 13
  are the pre-existing `DISABLED_` tests, the same set as the baseline
  (name-by-name diff identical). Wall time 684.5 s (baseline 710.0 s).
- **Accounting vs the 1506/1519 baseline** (name diff of the two ctest
  logs): **73 added, 0 removed**, all P12-NUM-004 suites:
  - `LinearFallbackTest` 13, `GMRESTest` 10, `SIMPLERobustnessTest` 10;
  - `AdaptiveRelaxation` 8, `ResidualTrend` 8, `RobustnessConfigTest` 6,
    `ResidualNormalization` 5, `ConvergencePolicy` 4;
  - `CompressibleSIMPLERobustnessTest` 3, `SolverRobustnessSettingsTest` 2,
    `ProjectRunnerTest` 2, `JSONWriterTest` 1,
    `SolverRobustnessPerformance` 1.

  1506 + 73 = 1579.
- **One pre-existing test was updated, and why.**
  `CaseValidationTest.UnsupportedLinearSolverTypeIsRejected` used `"GMRES"`
  as its example of an *unsupported* type. NUM-004 deliberately made
  GMRES supported, so the first full run failed it (1578/1579). The example
  became `"MINRES"` (still unsupported), which keeps the test's intent that
  unsupported types are rejected. The rejection itself is unchanged, and
  `RobustnessConfigTest.GmresLinearSolverTypeIsAccepted` also checks MINRES
  rejection. The rerun above is after that change.
- **Validation-file noise:** each full run rewrote the same 13 tracked
  `results/validation/**/validation.json` files (natural convection Ra1e3
  ×7, channel flow k-ε/k-ω/SST ×6), with **only** `"runtime_seconds"`
  changed (0 other lines). They were reverted with `git checkout` after
  each run.

## 20. Acceptance gates

| Gate | Result |
|---|---|
| Normalization: zero baseline handled | **PASS**: floor policy; 0 → 0, 1e-6 → 100 (finite); cavity v residual 0 at iteration 1 normalized 0 |
| Normalization: histories finite | **PASS**: every SIMPLE/Compressible test checks them; finite input can never yield inf (saturation) |
| Convergence decision quantitatively verified | **PASS**: normalized 754 vs absolute 1728 iterations, all gates hold at 754 and not at 753; no hidden convergence (continuity/global/NaN); scale invariance (181 = 181) |
| Stagnation: plateau detected before MaxIterations | **PASS**: genuine round-off plateau → Stagnated at 3151 (vs MaxIterations at 6000), identical final residuals |
| Stagnation: slow convergence not flagged | **PASS**: ρ = 0.999 synthetic runs on; the healthy cavity with every detector on is bit-identical; the slow pressure phase of the plateau run is not flagged |
| Divergence: finite runaway detected | **PASS**: aggressive 0.9/0.9 cavity → Diverging at iteration 20 (finite fields) vs growth to 5e58 and MomentumFailure at 90 undetected |
| Divergence: single spike does not trigger | **PASS**: 1e6 spike and W − 1 burst → no verdict |
| Adaptive relaxation: measurable benefit on a difficult case | **PASS**: fixed 0.9/0.9 diverges; adaptive **converges in 672** (faster than conservative fixed 1032). Compressible 296 vs 440; Arkilic 11302 vs 12189 with identical validation error |
| Linear fallback: real failure → fallback → success, outer continues | **PASS**: P12-COMP-002 BiCGSTAB breakdown at outer iteration 33 → SPD proven → CG converged → SIMPLE continues (8/8 recovered in 60 iterations); CompressibleSIMPLE likewise; the full Arkilic production case in its original BiCGSTAB configuration completes (2040 fallbacks, all recovered) with L2 1.2864% / L∞ 2.5519% |
| Never CG on an unsupported matrix | **PASS**: sufficient SPD test; unit-tested for indefinite, non-symmetric, singular and reducible matrices |
| Compatibility with features disabled | **PASS**: 2439-line library dump byte-identical; bit-identity tests; 1506 pre-existing tests pass (one example value updated, see §19) |
| Determinism | **PASS**: identical statuses, residual/relaxation/normalized histories and fallback choices on repeated runs |
| Performance | **PASS**: 1.0 µs per outer iteration bookkeeping; end-to-end ratio 0.985 |
| Full regression | **PASS**: 1579/1579 |
