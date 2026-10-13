# P17.8 — Density-Based Compressible Solver — SUMMARY

```text
STATUS   P17.8 COMPLETE
GATE     results/p17/density_based_solver/acceptance_gate.md
         sha256 a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e
         FROZEN before any implementation source existed, and UNMOVED since -- re-hashed at the
         start of every evidence-producing step in this phase, not only at the end.
SCOPE    Euler-only, host-only, library-level, restart UNSUPPORTED-and-refusing, three boundary
         treatments. Every one of those limits was decided in architecture.md BEFORE implementation
         and frozen with the gate; none was chosen after a measurement.
```

---

## 1. What now exists

A density-based compressible solver that evolves `Q = [rho, rhoU, rhoV, rhoW, rhoE]` directly, using
P17.7's HLLC flux for the inviscid face flux, an explicit single-stage update, and an explicit
CFL-controlled time step.

```text
include/cfd/compressible/DensityBasedSolver.hpp    new
src/compressible/DensityBasedSolver.cpp            new
tests/unit/compressible/test_density_based_solver.cpp          new, 32 TESTs
tests/integration/compressible/test_density_based_canonical.cpp new, 4 TESTs
```

It composes upstream physics and restates none of it: 0 equations of state, 0 sound-speed formulas,
0 energy relations and 0 Riemann solvers appear in either file, checked structurally against three
upstream files that must all match the same pattern for the zero to mean anything.

## 2. The headline numbers

```text
uniform-flow drift, all five quantities        0.000e+00      C1 grid-aligned, 23 steps
uniform-flow drift, oblique (u,v,w non-zero)   0.000e+00      C2, 23 steps
closed-box mass conservation                   8.310e-16      G1, 80 cells, 37 steps
closed-box energy conservation                 1.696e-16      G1
closed-box momentum vs the wall impulse        1.066e-14      against a change of 2.864e+01
open-domain mass balance                       3.816e-17      G2, 40 steps, bound 5.509e-13
open-domain momentum balance                   1.332e-14      bound 3.598e-10
open-domain energy balance                     3.365e-11      bound 2.401e-07
contact pressure / velocity error              7.181e-16 / 6.496e-16    bound 1e-9
smooth-wave L1 error                           1.038e-02 -> 5.993e-03 -> 3.255e-03
smooth-wave observed order                     0.7928 then 0.8804   REPORTED, not gated
smooth-wave p and u uniformity                 ~1e-15         the strong half of that result
two-cell residual vs a 50-digit reference      within 8 eps x sum_f|F_f|A on all 10 components
CFL <-> calculateCFL consistency               0.000e+00      bound 1e-12
dt linear in CFL                               ratio EXACTLY 2
SIMPLE fingerprint                             BITWISE IDENTICAL, 0-byte diff
repeated solve                                 BITWISE on dt, time, state, residual and retry
fallback activations                           0 -- there is no fallback flux
retries / rejections in valid cases            0 / 0
```

## 3. Four real defects, found by the gate

### 3.1 A sign error in P17.7's shipped HLLC star energy

```text
WRONG   rhoE*_K = rho*_K [ rhoE_K/rho_K + (S* - u_nK)(S* MINUS p_K/(rho_K(S_K - u_nK))) ]
RIGHT                                                    PLUS
```

Present in the C++ **and** in P17.7's frozen 60-digit independent reference — which is why P17.7's
X2 criterion measured agreement to 3.25e-16 while both were wrong. An independent reference has to
be independent of the author's *understanding*, not just of the code.

P17.8 found it because an inviscid wall is the one cheap configuration with `S* = 0` while
`u_n != 0`, so the erroneous term is not multiplied by zero. P17.7 had no boundary conditions, so
the defect lived exactly in the gap between the two phases' scopes. Wall energy flux
`2.645e+07 -> 7.45e-09 W/m²`. Corrected, derived from Rankine–Hugoniot, and now guarded by a
property neither artefact can satisfy by accident (`F*_energy == S*(rhoE* + p*)`), which P17.8's own
reference script enforces on every run.

**Criterion A6 ("every P17.1–P17.7 production source UNTOUCHED, 0 TOUCHED") is NOT met as written.**
See `AMENDMENT_A6_upstream_exception.md`.

### 3.2 `solve()` did not catch an overflow from `assembleResidual`

`toPrimitive` and the dt evaluation were both wrapped; the residual assembly was not. A flux that
*overflows* (as distinct from coming out NaN) is caught inside P17.7's `hllcFluxChecked`, which
throws — so it escaped `solve()` as an uncaught exception instead of a `NonFiniteState` with a
location. Found by criterion P5. The diagnostic now names the iteration, the face and the owner
cell.

### 3.3 `offendingCell` disagreed with its own diagnostic

On the recover-failure path the field was hard-coded to 0 while the message named the real cell.
Criterion P3's test asserted the message, so the field went untested. Both now come from one source.

### 3.4 Three criteria had a test bearing their name that could not fail

```text
U1   ran on createCartesian2D(3, 2, 3.0, 2.0) -- dx = dy = 1, so every cell volume was EXACTLY 1.0
     and dt/V was bit-identical to dt. Control N2 deletes the division by volume; U1 PASSED.
     Now on a graded mesh, asserting that no cell has unit volume, plus a 1/V ratio assertion.
G1   named sum_P rhoU V; the test computed the x-momentum and only printf'd it. Momentum is not
     constant in a closed box with a non-uniform pressure field -- the walls exert a net force --
     so the correct statement is the BALANCE, and it now is one.
C1/C2 named "rho, rhoU, rhoE, p, T"; the tests asserted the three conserved ones only.
```

All three were found by running the controls and then re-reading the frozen gate against the tests,
not by the tests themselves. That is the recurring lesson of this phase and of P17.7's amendment:
**a criterion's non-vacuity is not the same as its coverage.**

## 4. Two frozen Group N predictions settled by algebra

Recorded in `AMENDMENT_N2_N3_control_predictions.md`; the frozen gate is not edited.

```text
N2 must break C1   -- IMPOSSIBLE. A uniform state's residual is identically zero, so the update is
                      the identity for ANY scale. No implementation of C1 can detect a 1/V error.
N2 must break G1   -- IMPOSSIBLE ON A UNIFORM MESH. The constant volume factors out of both sides.
                      Closed by ADDING a graded-mesh case, which detects it.
N3 must still pass K3 -- IT DOES NOT, and that is correct: with no acoustic term a cell at rest has
                      no signal speed at all, so no dt exists. The control breaks MORE than
                      predicted, which strengthens the instrument rather than weakening it.
```

## 5. SIMPLE preservation, and the demonstration no earlier P17 phase ran

```text
A3  P13-001 SIMPLE fingerprint: BITWISE IDENTICAL, 10 cases / 130 lines, 0-byte diff
A1/A2/A5/A6  27 files verified UNTOUCHED by SHA-256, one declared exception (3.1)
A4  0 references to the solver from any SIMPLE or pressure-based path
```

G1 requires the fingerprint instrument to be *shown able to fail*. A grep across every P17 script
that drives the probe finds **no perturbation in any of the eight earlier phases** — each reported
"BITWISE IDENTICAL" without demonstrating the instrument could say anything else. P17.8 plants a
0.1 % change in the SIMPLE velocity relaxation, rebuilds, and gets **112 of 130 lines differing**,
then restores and verifies the restore by hash (the script aborts if it does not match).

## 6. Regression — every predicted count matched exactly

```text
                          release_cuda          cpu_only
--------------------------------------------------------------------
registered                2732  PRED 2732       2526  PRED 2526
exclusions                  13  PRED 13           13  PRED 13
EXECUTED                  2670  PRED 2670       2466  PRED 2466
passed                    100%                  100%
FAILED                       0  PRED 0             0  PRED 0
Not Run                      0                     0
staleness (fail closed)   48 bins, 0 stale      47 bins, 0 stale
runtime                   2282 s                1648 s
--------------------------------------------------------------------
NEW FAILURES: none.   KNOWN FAILURES: none.
P17_CODE_FINGERPRINT unmoved across the regression:
  577cb482945440af848b5886f6b3a02d699cb202f557d05b780bad6ef176f8a9

Named rows EXECUTED (registration is not execution):
  DensityBased 36 / 36      Hllc 39 (release) and 35 (cpu-only; the 4 GPU rows need CUDA)
  IdealGasEOSTest 30 / 30   LowMach 25
```

The `Hllc` rows are load-bearing here and not just P17.7 preservation: P17.8 changed
`HllcFlux.hpp`, and all 39 rows — including the 4 GPU bitwise-equivalence rows — pass on the
corrected source. That works because the `.cu` contains 0 formulas, so both paths received the same
change from the same line.

Details, including the environment actually used: `regression.md`.

## 7. Gate verdict

```text
G1  baseline preservation          PASS   fingerprint BITWISE, 0-byte diff, instrument shown able
                                          to fail (112 of 130 lines on a planted 0.1 % change)
G3  variable-density conservation  PASS   closed box 8.310e-16, open domain 3.816e-17
G4  total-energy conservation      PASS   closed box 1.696e-16, open domain 3.365e-11
G6  HLLC in solver integration     PASS   R1 bitwise, R2 vs an independent 50-digit reference,
                                          C1/C2/C3/C4 -- and it CORRECTED a defect in the flux
G9  deterministic repeatability    PASS   repeated solve BITWISE on every history
G7  shock validation               NOT CLAIMED -- P17.9's
G8  CPU/GPU solver equivalence     NOT IMPLEMENTED -- no GPU density-based solver exists
G10 full qualification             NOT CLAIMED -- P17.11's
```

```text
ONE CRITERION IS NOT MET, and it is reported as not met rather than reinterpreted:
  A6  "every P17.1-P17.7 production source UNTOUCHED, 0 TOUCHED"
HllcFlux.hpp changed, to fix a real sign error P17.8 found in it. See section 3.1 and
AMENDMENT_A6_upstream_exception.md.
```

## 8. What is NOT claimed

```text
G7 shock validation        NOT CLAIMED. No Sod tube, no normal shock, no shock-position or
                           jump-ratio measurement. P17.9 owns those and §49 forbids starting them.
G8 CPU/GPU solver          NOT IMPLEMENTED. P17.7's FLUX is bitwise identical on both, and was
                           re-verified after the sign fix; the residual assembly, the CFL reduction
                           and the update are host-only. Reported NOT IMPLEMENTED, never PASS.
compressible Navier-Stokes NOT CLAIMED. Euler only -- no viscous stress, no conduction, and NO ZERO
                           STUB, because a zero stub is indistinguishable from a wired-up term that
                           happens to vanish.
production reachability    NOT CLAIMED. Library level: no dispatch change, no case-format key. A
                           case file cannot select this solver. That is also WHY SIMPLE is provably
                           unaffected.
restart                    UNSUPPORTED, and it REFUSES, naming the conserved fields the format
                           cannot carry. Restart determinism (§30) is therefore untested.
subsonic open boundaries   NOT IMPLEMENTED. Wall, supersonic inflow, supersonic outflow only.
order of accuracy          C4's 0.79 -> 0.88 is a measured order on one smooth case, REPORTED. No
                           Richardson extrapolation, no GCI, no formal order claim.
low-Mach performance       NOT CLAIMED. The explicit step saturates at the acoustic limit as
                           Ma -> 0 (measured: dt ratio 1.0989 over two decades of Mach). P17.6's
                           CompressibleSIMPLE remains the low-Mach reference.
Debug+GUI, sanitizers      NOT RUN -- deferred to P17.11 by standing decision, matching every
                           earlier P17 phase.
```

## 9. Recorded, not fixed

```text
* DensityBasedStatus::Stagnated is declared and never assigned -- unreachable today. Removing a
  public enumerator is an API change outside P17.8's scope.
* 22 files fail tree-wide clang-format. All PRE-EXISTING, none of them P17.8's; standing debt first
  recorded in P17.1. P17.8's own files are clean.
* P17.7's verification.md energy figures for the strong-discontinuity rows were wrong and are
  corrected in its amendment (0.2961 -> 0.01253 for Sod, and similarly for two others). Whether to
  reopen P17.7's evidence is the user's decision, not P17.8's to take.
```
