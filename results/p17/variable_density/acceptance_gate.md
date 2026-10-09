# P17.1 — Variable Density: acceptance gate

**Frozen 2026-09-26, after the dry-run and before any production edit.** Every baseline
below was *measured* on the unchanged tree and is recorded in `logs/00_dryrun.log`,
never inferred. Stop at the first failure. No threshold may be weakened; a mis-derived
criterion is preserved and amended in a separate file.

Dry-run: `logs/00_dryrun.log`. Design decision: `design.md`.

---

## 0. What this item changes, and what it does not

P17.0 proved eleven of thirteen P17.1 rows already exist. The **only** production
change is:

```text
src/compressible/CompressibleMassFlux.cpp
  validateFaceDensityInputs -- additionally validate density VALUES (finite, > 0),
  naming the offending cell, throwing InvalidArgumentError. No clamping.
```

Everything else in this gate is **verification of existing behaviour** plus new tests
and conservation evidence.

### Two deliberate scope boundaries, decided before implementing

```text
NOT validating temperature or pressureGauge VALUES here.
  Reason, found by reading the tests rather than assumed: the existing
  CompressibleMassFluxTest.ThrowsOnNonPositiveBoundaryTemperature sets
  temperature[0] = -1.0 and relies on the BOUNDARY EOS evaluation to reject it. An
  up-front temperature check would make that test throw earlier, so it would still
  pass while no longer exercising the path it was written for -- a contaminated
  instrument (CLAUDE.md section 5). The EOS already validates T and p and is proven to
  do so. Duplicating it would blunt a working test for no gain.
NOT adding a positivity LIMITER / clamp.
  That is a conserved-state concern and belongs to P17.8. See design.md section 3.
```

## 1. Measured baseline — the dry-run record

```text
mesh                     createCartesian2D(2,1,2.0,1.0)   cells 2, faces 7
density-VALUE checks     0 in all five compressible modules
negative DENSITY tests   0            (size-mismatch tests present: 4)
EOS at reference         R=287.05000000000001  cp=1005  cv=717.95000000000005
                         gamma=1.3998189288947698
                         rho(172230, 300)   = 2
                         rho(344460, 300)   = 4
                         rho(172230, 600)   = 1
                         rho(101325, 288.15)= 1.2250122659906946
dRho/dp at reference     1.161237879579632e-05      POSITIVE
dRho/dT at reference     -0.0066666666666666671     NEGATIVE
rho(2p,T)/rho(p,T)       2      exactly
rho(p,2T)/rho(p,T)       0.5    exactly
nonuniform {1.0, 3.0}    internal rho_f = 2, mdot_f = 2      (arithmetic mean)
uniform rho = 2.0        internal rho_f = 2, mdot_f = 2
compressible vs incompressible at uniform rho:  0 differing faces, BITWISE
```

### The gap, measured — and worse than "NaN propagates"

```text
input            internal rho_f   mdot_f   non-finite mdot?   outcome
-----------------------------------------------------------------------------
rho[0] = -2.0         0             0            NO           RETURNED
rho[0] =  0.0         1             1            NO           RETURNED
rho[0] =  NaN        nan           nan           yes          RETURNED
rho[0] = +Inf        inf           inf           yes          RETURNED
rho[0] = -Inf       -inf          -inf           yes          RETURNED
```

All five **returned** rather than throwing, so the gate is non-vacuous.

**The two that matter most are the finite ones.** A negative density averaged against
a valid one gives `rho_f = 0` exactly, so the face silently carries **no mass**; a zero
density gives `rho_f = 1`, a perfectly plausible-looking flux from an impossible state.
Neither is non-finite, so `CompressibleSIMPLE`'s existing `allFinite()` sweep — the
only downstream protection — **cannot catch either**. That is why input validation is
the right layer, and it is the measurement that justifies this item existing at all.

## 2. Group D — density bounds and invalid-state handling  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| D1 | A non-positive cell density is rejected by `calculateCompressibleMassFlux`. | throws `InvalidArgumentError`, boolean |
| D2 | A non-finite cell density (NaN, +Inf, -Inf) is rejected. | throws, boolean |
| D3 | `evaluateCompressibleFaceDensity` rejects the same inputs — the shared validator, not only the outer entry point. | throws, boolean |
| D4 | The message names the offending **cell index**, as `evaluateDensityField` already does. | substring match on the index |
| D5 | The message names the field, as `IdealGasEOS` already does. | substring `density` |
| D6 | `rho = 0` exactly is rejected. The boundary is `> 0`, not `>= 0`. | throws, boolean |
| D7 | Rejection is by **exception**, never by clamping: no call returns a modified density. | no clamp path exists |

**No tolerance anywhere in Group D.** Every criterion is a boolean — either it throws
or it does not. There is nothing to weaken, which is the point.

## 3. Group S — spatial variation is real  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| S1 | With `rho = {1.0, 3.0}` the internal face density is the arithmetic mean. | **exactly 2.0**, dyadic |
| S2 | The mass flux uses the local face density, not a global constant. | `mdot_f` exactly `rho_f * U * A` |
| S3 | Distinct cells hold distinct densities through a full solve — density does not collapse to a scalar. | `max(rho) - min(rho) > 0` |
| S4 | Doubling one cell's density changes only the faces adjacent to it. | untouched faces bitwise identical |

S1 is **exact, not toleranced**: the mesh is uniform so the interpolation weight is
exactly 1/2, and `(1.0 + 3.0)/2 = 2.0` is exactly representable. A dyadic control that
must give exactly the right answer, per `CLAUDE.md` §5.1.

## 4. Group T — pressure / temperature coupling  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| T1 | `rho = p/(R T)` at representative states. | relative error <= 1e-12 |
| T2 | `dRho/dp > 0` at fixed T. | sign, boolean |
| T3 | `dRho/dT < 0` at fixed p. | sign, boolean |
| T4 | Doubling p at fixed T doubles rho. | **exactly 2.0** |
| T5 | Doubling T at fixed p halves rho. | **exactly 0.5** |
| T6 | Simultaneous spatial variation in p and T gives the per-cell EOS value in every cell. | relative error <= 1e-12 |

T4 and T5 are exact because a factor of two is dyadic: `p/(R T)` scaled by 2 in the
numerator or denominator is exactly representable, and the dry-run measured exactly
`2` and `0.5`. Requiring a tolerance there would be looser than the arithmetic
actually is.

T1/T6's **1e-12** is inherited from P17.2's frozen gate, where it was derived from two
roundings of a double (~4500x the double epsilon, four orders tighter than the existing
`EosConsistencyErrorIsAtFloatingPointTolerance` 1e-9) and proved load-bearing by a
control that perturbed `R` by 1e-6. Not re-derived, not loosened.

## 5. Group C — the constant-density limit is PRESERVED  (PRESERVATION)

| id | criterion | bound |
| --- | --- | --- |
| C1 | Compressible mass flux at uniform rho equals `physics::calculateMassFlux` with the same constant. | **BITWISE**, 0 differing faces |
| C2 | Incompressible SIMPLE remains structurally independent of the EOS. | **0** references to `EquationOfState`/`IdealGasEOS`/`ThermodynamicProperties` under `src/pressure_velocity/` and `include/cfd/pressure_velocity/` |
| C3 | The P13-001 SIMPLE fingerprint is unchanged. | **BITWISE**, 10 cases / 130 lines |
| C4 | A constant density stays constant through a full compressible solve. | `max - min == 0` exactly |

C1 was measured at **0 differing faces** in the dry-run, so it is a criterion the
baseline demonstrably satisfies and the change must not break. C2 is a structural
criterion rather than a numerical one: it makes "incompressible cannot become
EOS-dependent" checkable by grep rather than by argument.

## 6. Group M — mass flux  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| M1 | `m_dot_f = rho_f (U_f . n_f) A_f` against an independently computed expectation. | relative error <= 1e-12 |
| M2 | Reversing velocity reverses the flux sign, same magnitude. | **exact** negation |
| M3 | Nonuniform density changes the flux exactly as the face density changes. | relative error <= 1e-12 |
| M4 | A boundary face's density is the EOS at that face's own interpolated p and T, not the owner cell's. | relative error <= 1e-12 |
| M5 | Face density is recomputed per call, never stale: mutating the cell field changes the next call's flux. | flux differs |
| M6 | Wall faces carry exactly zero flux regardless of density. | **exactly 0** |

## 7. Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Remove the density-value validation entirely. | D1, D2, D3, D6 |
| N2 | Validate finiteness only, dropping the `> 0` test. | D1 and D6, while D2 still passes — **this is the sharpest control**: it proves the positivity clause is what detects the silent `rho_f = 0` case, not the finiteness clause |
| N3 | Validate `>= 0` instead of `> 0`. | D6 only — proves the boundary is at zero, not near it |
| N4 | Make the internal face density a global constant instead of an interpolation. | S1, S2, S3, S4, M3 |
| N5 | Replace the compressible face density with `fluid.density()`. | S1, M3, M4 — while C1 still passes, proving C1 alone cannot detect a collapse to constant density |

N2 and N5 are the two that prove criteria rather than plumbing. N2 targets the exact
failure the dry-run measured (finite, silent, wrong). N5 guards against the trap that a
constant-density equivalence check is satisfied *by* a constant-density bug.

## 8. Group G — preservation gate

| id | criterion |
| --- | --- |
| G1 | The existing compressible suite passes — 129 registered tests. |
| G2 | The EOS and thermodynamics tests pass — 21 + 7. |
| G3 | `ThrowsOnNonPositiveBoundaryTemperature` still passes **and still reaches the boundary EOS** (see §0). |
| G4 | P14 transport preservation — 68, the frozen narrow regex. |
| G5 | P15 thermal preservation — 161. |
| G6 | P16 turbulence preservation — 315. |
| G7 | The P13-001 SIMPLE fingerprint is BITWISE identical. |
| G8 | Full Release+CUDA regression: **2497 executed, 0 failed**, plus the new tests. |
| G9 | Full CPU-only regression: **2297 executed, 0 failed**, plus the new tests. |
| G10 | `P17_CODE_FINGERPRINT` moves **only** by the authorized delta, attributed file by file. |

G8/G9 counts are the P17.0 baseline measured in `results/p17/baseline/`. The expected
new totals are `2497 + <new tests>` and `2297 + <new tests>`, predicted exactly before
the run in `regression.md`.

## 9. Group K — conservation  (DETECTION)

| id | criterion | bound |
| --- | --- | --- |
| K1 | Closed domain: net boundary mass flux is zero. | `<= bound`, derived below |
| K2 | Through flow, converged: `mdot_in + mdot_out = 0`. | `<= bound` |
| K3 | Per-cell continuity imbalance for a divergence-free constructed field. | `<= bound` |
| K4 | **Variable-density** case: the same bounds hold with a genuinely spatially varying rho. | `<= bound` |
| K5 | K4 would FAIL if density were internally constant. | a control, must fail |

### Deriving the bound — not choosing it

The imbalance is a sum of `N_f` face fluxes, each of magnitude at most
`max|mdot_f|`. Floating-point summation of `N` terms accumulates a relative error
bounded by `~N * eps` in the worst case, so the dimensional floor is:

```text
bound = kAccum * eps * N_f * max|mdot_f|

eps          = 2.220446049250313e-16   (double)
N_f          = the actual face count of the case, so the bound SCALES with the mesh
max|mdot_f|  = measured from the case itself, so the bound SCALES with the flux
               magnitude -- never an absolute constant
kAccum       = 8, EMPIRICAL, labelled as such per CLAUDE.md section 5.1. It is a
               safety factor on the summation order, not a fitted value: pairwise or
               sequential summation differ by a small constant, and 8 covers both
               without being large enough to admit a real imbalance. The measured
               imbalance is reported alongside the bound so the margin is visible
               rather than asserted.
```

This follows the established precedent: the existing `GlobalMassImbalanceIsSmall`
already uses a **computed** bound rather than a literal, and
`fluxScale = max(1e-9, rhoAvg * U_mean * H)` is its analogue of the scale factor above.

**K5 is what makes Group K mean anything.** A conservation test on a variable-density
case that would also pass with constant density internally measures nothing about
variable density. The control must fail.

## 10. Gate verdict

P17.1 closes only when **D, S, T, C, M, G and K all pass and every N control fails**,
with the source delta attributed file by file and both fingerprints recorded.

```text
FORBIDDEN
  weakening any bound above, including kAccum
  adding a clamp to make an invalid state "work"
  changing ThrowsOnNonPositiveBoundaryTemperature
  touching the incompressible path
  implementing the dilatational stress term (P17.4), two-way energy (P17.5),
  or a positivity limiter (P17.8)
```
