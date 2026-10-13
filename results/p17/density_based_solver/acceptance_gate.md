# P17.8 — acceptance gate

**FROZEN before any implementation source was written.** Every threshold is exact (bitwise/boolean) or
derived from a stated analysis. None is fitted to an observed value.

`eps = 2.2204460492503131e-16`. Scope decisions are in `architecture.md` and are part of this freeze:
**Euler-only**, **no GPU solver**, **restart UNSUPPORTED with an explicit refusal**, **library-level
(not case-reachable)**, three boundary states only.

---

## 1. Group A — SIMPLE preservation (§3, §28) — a HARD requirement

| id | criterion | threshold |
| --- | --- | --- |
| A1 | `src/app/ProjectRunner.cpp` UNTOUCHED — no dispatch change | SHA-256 identical |
| A2 | `SIMPLE.cpp/.hpp`, `CompressibleSIMPLE.cpp` UNTOUCHED | SHA-256 identical |
| A3 | P13-001 SIMPLE fingerprint | **BITWISE IDENTICAL**, 0-byte diff |
| A4 | 0 references to the density-based solver from any SIMPLE or pressure-based path | exactly 0 |
| A5 | `solver/CFL.hpp`, `solver/CFL.cpp`, `solver/TransientSolver.*` UNTOUCHED — extended, never modified | SHA-256 identical |
| A6 | every P17.1–P17.7 production source UNTOUCHED | 0 TOUCHED |

## 2. Group Q — the conserved state and its conversions (§4, §5)

| id | criterion | threshold |
| --- | --- | --- |
| Q1 | `fromPrimitive` → `toPrimitive` round trip | `rho` and `rhoE` **BITWISE**; `T`, `p` within `8 eps` (they pass through the closed-form inverse and the EOS) |
| Q2 | **no duplicate conversion or EOS in the solver**: 0 occurrences of a pressure formula, a sound-speed formula, `(gamma-1)`, or an energy relation in `DensityBasedSolver.*` | exactly 0 |
| Q3 | the conversions are P17.5's: `conservedTotalEnergy` / `recover` are the only call sites | ≥ 1 each, and 0 hand-rolled alternatives |
| Q4 | primitives are **not stored** beside `Q` — recomputed on demand, so they cannot drift | structural |

## 3. Group R — the residual (§6, §7, §34)

| id | criterion | threshold |
| --- | --- | --- |
| R1 | for every internal face, the owner and neighbour contributions cancel: all 5 components | **BITWISE** — the same value with opposite signs, DERIVED below |
| R2 | a two-cell domain's residual matches an independently hand-computed value | `≤ 8 eps × |F| A` |
| R3 | **one** HLLC evaluation per face produces all five components | structural: exactly 1 `hllcFlux` call site in the assembly loop |
| R4 | a closed uniform domain has zero residual | `≤ 16 eps × max_f(|F_f| A_f) × maxFacesPerCell`, DERIVED below |
| R5 | the sign convention matches the repository's (`+F` owner, `−F` neighbour) | structural, against `CompressibleContinuity.cpp` |

```text
DERIVATION OF R1's BITWISE CLAIM. The assembly computes ONE FluxVector per face and adds it to the
owner and subtracts the SAME value from the neighbour. Negation of a double is exact, so the two
contributions are exact negations by construction -- not to round-off. Any tolerance here would hide a
second flux evaluation, which is exactly what R3 forbids.

DERIVATION OF R4's BOUND. For a uniform state every face carries the physical flux F(Q). Summing over
a closed cell gives F(Q) . sum_f(A_f n_f) = F(Q) . sum_f(areaVector_f), and sum of a closed cell's area
vectors is zero. In floating point the cancellation is not bitwise because the accumulation order is
face-id order, not paired; each partial sum carries at most 1 ulp of the running magnitude, so with at
most `maxFacesPerCell` additions the error is bounded by ~n_faces eps max|F A|. 16 eps x that, with a
factor for the five components, is the bound. It is a BOUND, not a measurement.
```

## 4. Group K — CFL and the time step (§10, §11, §36)

The implemented formula, fixed here:

```text
  dt_P = CFL * V_P / sum_over_P's_faces( (|U_f . n_f| + a_f) * A_f )
  with the FACE state taken as the arithmetic mean of the two cell primitive states (the interior
  state alone at a boundary face), and a_f the mean sound speed. GlobalPhysical: dt = min_P dt_P.
```

| id | criterion | threshold |
| --- | --- | --- |
| K1 | `dt` is exactly linear in `CFL`: `dt(2C) / dt(C) == 2` | **BITWISE** — `CFL` multiplies a quantity it does not otherwise enter |
| K2 | raising the velocity lowers `dt`; raising `T` (hence `a`) lowers `dt`; shrinking the cell lowers `dt` | strict inequality, each isolated |
| K3 | `dt > 0` and finite for every valid state, including `U = 0` | boolean |
| K4 | **CONSISTENCY WITH THE EXISTING `calculateCFL`**: in the limit `a → 0` with uniform density and a uniform grid-aligned flow, feeding P17.8's `dt` back through `solver::calculateCFL` returns the requested CFL | `≤ 1e-12` relative, DERIVED below |
| K5 | invalid `CFL` (`≤ 0`, NaN, Inf) throws `InvalidArgumentError` naming the parameter | throws |
| K6 | edge cases — zero velocity, Mach ~3, `p` to 1e-3, cells spanning 1e-4…1e2 — all give finite positive `dt` | boolean |

```text
K4 IS THE CRITERION THAT PROVES THE NEW CFL EXTENDS THE OLD CONVENTION rather than competing with it,
and it is the reason the audit's "NEEDS EXTENSION" classification is honest.
The two formulas are:
    existing   Co_P = (dt / (2 V_P)) * sum_f |massFlux_f| / rho ,  massFlux_f = rho (U_f.n_f) A_f
    new        dt_P = CFL * V_P / sum_f (|U_f.n_f| + a_f) A_f
Setting a = 0 and uniform rho, the new one gives sum_f |U_f.n_f| A_f = CFL V_P / dt, while the existing
one gives Co = dt/(2 V_P) * sum_f |U_f.n_f| A_f = CFL/2. So the EXPECTED round trip is Co = CFL/2, and
the factor 2 is the existing header's documented "1/2 factor ... keeps this equal to the 1D convention
Co = U dt/dx". The criterion therefore asserts Co == CFL/2, not Co == CFL -- getting that factor wrong
is precisely the confusion the criterion exists to catch. The 1e-12 tolerance covers the face-averaging
difference (the existing function uses the authoritative mass flux; the new one averages cell states),
which vanishes only for an exactly uniform field -- so the test uses a uniform field and the residual
tolerance is for accumulation order alone.
```

## 5. Group U — the update and the two modes (§9, §12, §13, §35)

| id | criterion | threshold |
| --- | --- | --- |
| U1 | for an injected known residual `R`, `Q_new == Q_old − (dt/V) R` component by component, all 5 | **BITWISE** |
| U2 | `GlobalPhysical`: every cell advances with the SAME `dt`, equal to `min_P dt_P` | **BITWISE** equality across cells |
| U3 | `LocalPseudoTime`: cells advance with DIFFERENT `dt`, and the result reports **no physical time** | boolean; the accessor must refuse |
| U4 | the mode is an explicit setting; there is no automatic switching | structural: 0 mode assignments outside construction |

```text
U3 IS HOW §12's WARNING IS ENFORCED RATHER THAN MERELY WRITTEN DOWN. Local pseudo-time is not a
transient integrator -- cells advance different amounts of physical time -- so the result type refuses
to report `time` in that mode instead of returning a number that means nothing.
```

## 6. Group P — positivity of the UPDATED state, and robustness (§18, §19, §20)

| id | criterion | threshold |
| --- | --- | --- |
| P1 | after every candidate update: `rho > 0`, `Q` finite, and `recover()` succeeds (so `p > 0`, `T` in range, valid internal energy) — checked by **P17.5's model**, not a second opinion | boolean, every cell |
| P2 | an invalid candidate triggers a DETERMINISTIC retry: `dt ← dt × 0.5`, up to 5 times | exact counts reported |
| P3 | terminal failure returns `InvalidState` naming the offending **cell id** and its values | boolean |
| P4 | **NOTHING IS CLAMPED**: 0 clamp/floor idioms in the solver | exactly 0 |
| P5 | non-finite detection covers `rho`, each momentum component, `rhoE`, `p`, `T`, each residual component and `dt`; reports the FIRST offending cell in deterministic id order | boolean |
| P6 | retries and rejections are COUNTED and reported | a solve that shrank `dt` cannot look clean |

```text
WHY NOT CLIPPING. §18 permits it only with its conservation impact documented. The impact is that
clipping ADDS mass or energy the solver did not compute, which would silently break the G3/G4
conservation criteria this same phase asserts. So it is not done, and that is the reason rather than a
preference. Control N4 plants clipping and must break conservation.
```

## 7. Group C — the canonical cases (§21, §22, §23, §24)

| id | criterion | threshold |
| --- | --- | --- |
| C1 | **uniform flow, grid-aligned**, 20 steps: `rho`, `rhoU`, `rhoE`, `p`, `T` unchanged | `≤ 64 eps × the quantity`, DERIVED below |
| C2 | **uniform flow, oblique velocity** (`u ≠ 0`, `v ≠ 0`, `w ≠ 0`), 20 steps | same bound — this is P17.7's rotational consistency at solver level |
| C3 | **advected contact** (`p_L = p_R`, `u_L = u_R`, `rho_L ≠ rho_R`): `p` and `u` stay constant | `p`: `≤ 1e-9` relative; `u`: `≤ 1e-9` relative — DERIVED below |
| C4 | **smooth wave**: a linear acoustic pulse, compared against its analytic propagation | reported with its measured order; NOT gated on an invented order |

```text
DERIVATION OF C1/C2's 64 eps. A uniform state has zero residual only to R4's bound, so each step adds
at most R4's error / V_P * dt to the state. Over `nSteps` steps the drift is bounded by
nSteps x 16 eps x max|F A| x nFacesPerCell x dt / V. With the case's own dt (CFL 0.4) and 20 steps this
evaluates well below 64 eps of the state; 64 eps is the rounded bound, declared before measurement.

DERIVATION OF C3's 1e-9. A contact is an EXACT solution of the Euler equations with uniform p and u,
and HLLC resolves a contact EXACTLY (P17.7 measured 0 error for both stationary and moving contacts).
So p and u are preserved by the FLUX exactly; what perturbs them is only the update's own round-off
accumulated over the steps, plus the fact that the contact's DENSITY profile smears -- which does not
feed back into p or u for a pure contact. 1e-9 is therefore extremely loose relative to what is
expected (round-off), and is set there deliberately: a tight bound would be untestable if the smeared
density did couple back, and the criterion's purpose is to catch a scheme that puts a SPURIOUS pressure
or velocity jump at a contact -- an O(1) failure, not an O(eps) one. This is a DETECTION threshold, not
an accuracy claim, and it is labelled as such.
```

## 8. Group G — conservation (§25, §26) — gate criteria G3 and G4

| id | criterion | threshold |
| --- | --- | --- |
| G1 | a **closed** domain (inviscid walls all round): `sum_P rho V`, `sum_P rhoU V`, `sum_P rhoE V` unchanged over 20 steps | `≤ 8 eps × N_cells × the total`, the accumulation bound |
| G2 | an **open** domain: `Δ(sum_P Q V) + integrated boundary flux == 0` | `≤ 8 eps × N_faces × the flux scale` |
| G3 | the per-face cancellation (R1) is what makes G1 exact rather than approximate | cross-reference, not a separate measurement |

## 9. Group M — residual norms and convergence (§16, §17, §37)

| id | criterion | threshold |
| --- | --- | --- |
| M1 | five norms exist, one per conserved equation, volume-weighted L2 | structural |
| M2 | the norm does NOT scale with mesh size: refining a uniform-state mesh leaves it at round-off | `≤ 64 eps` at both resolutions |
| M3 | the reference scale is captured ONCE at iteration 0 and never re-captured | structural; control N5 plants re-capture |
| M4 | convergence needs ALL of: five relative residuals, the mass imbalance, AND the state-change threshold | boolean — no single signal suffices |
| M5 | `GlobalPhysical` reaching the end time reports `TimeComplete`, a DIFFERENT status from `Converged` | exact status |
| M6 | steady criteria are evaluated ONLY in `LocalPseudoTime` mode | structural |

## 10. Group D — determinism (§31) — gate criterion G9

| id | criterion | threshold |
| --- | --- | --- |
| D1 | a repeated solve: `dt` history, `Q`, `p`, `T`, all five residual histories, iteration count, retry count, rejection count | **BITWISE**, `EXPECT_EQ` |
| D2 | control-flow decisions (retry, rejection) are deterministic — the same solve retries the same number of times | **BITWISE** |

## 11. Group S — scope boundaries that must be REFUSED, not silently wrong (§29)

| id | criterion | threshold |
| --- | --- | --- |
| S1 | `restartSupported()` returns **false** | boolean |
| S2 | a restart request **throws**, naming the conserved fields the format cannot carry | throws, with the names in the message |
| S3 | no viscous or conduction term is present, and none is stubbed with zeros pretending to be physical | structural: 0 viscous terms, and the absence is documented |
| S4 | no GPU density-based solver is claimed | reported NOT IMPLEMENTED, never PASS |

## 12. Group N — non-vacuity controls

Each must **FAIL** the named criteria and be restored with SHA-256 verification.

| id | planted defect | must break | must still pass |
| --- | --- | --- | --- |
| N1 | the neighbour's `−F` contribution dropped | R1, G1, G2, C1 | Q1, K1 |
| N2 | the update uses `dt` instead of `dt/V` | U1, C1, G1 | R1 |
| N3 | the acoustic term `+a_f` dropped from the CFL | K4 (and the expected `CFL/2` relation moves) | K1, K3 |
| N4 | density **clamped** to a floor instead of retrying | G1, G2, P4 | C1 (a valid case never clamps) |
| N5 | the residual reference scale re-captured every iteration | M3 — the relative residual becomes ~1 forever, so convergence never triggers | M1 |
| N6 | the wall exterior state does NOT mirror the normal velocity | the wall mass-flux criterion, G1 | C1 (no wall in the periodic case) |
| N7 | `min_P dt_P` replaced by the FIRST cell's `dt` | U2 | K1, K3 |

## 13. Execution policy — FROZEN, inherited verbatim

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
5 exclusion patterns, 13 tests, unchanged since P13's G10 closeout
build/qual-final (Release+CUDA+OpenMP) and build/qual-cpuonly (CUDA OFF)
build/qual-debug is NOT run -- deferred to P17.11 by standing decision
baseline, the P17.7 close:  Release+CUDA 2696 / 2634     CPU-only 2490 / 2430
the delta is declared in logs/02_freeze.log once the test count is fixed, and is pure arithmetic
```

## 14. Gate mapping and what is NOT claimed

```text
P17_GATES.md assigns P17.8:  G1  G3  G4  G6  G9
  G1  baseline preservation      -> Group A
  G3  variable-density conservation -> Group G (mass)
  G4  total-energy conservation  -> Group G (energy)
  G6  HLLC correctness in solver integration -> Groups R, C
  G9  deterministic repeatability -> Group D
G8 is NOT in P17.8's row and no GPU solver is built -- Group S4.
G7 SHOCK VALIDATION IS NOT CLAIMED. §49 and §42 both forbid it; the advected contact and any short
discontinuity run are SOLVER-INTEGRATION tests, not benchmarks. P17.9 owns Sod and the normal shock.
```

## 15. Failure policy

```text
STOP at the first failing criterion. Preserve the log. Diagnose. Fix the CODE, never the criterion.
No threshold here may be changed to obtain a PASS. Post-freeze corrections go in an AMENDMENT_*.md
with the reason and the measurement, and this file is left untouched.
A criterion-to-test coverage map with an "asserted?" column is required in coverage_map.md, kept
OUTSIDE this frozen file so completing it cannot move this hash.
```
