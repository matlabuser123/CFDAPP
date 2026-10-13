# P17.5 acceptance gate — FROZEN

Frozen before any total-energy production code exists. Thresholds are derived from
`logs/01_dryrun.log`, measured on the unchanged tree; none is a round number chosen for comfort.

**This gate carries a mandatory CRITERION → TEST coverage map (§13).** P17.4 closed with seven
frozen criteria that were measured by nothing or by the wrong instrument, five of them found by
hand and late. Every criterion below names the test that measures it, and any criterion whose cell
is empty is an unimplemented criterion, not a passing one.

---

## 0. Scope, stated before the criteria so the criteria can be read against it

```text
IN SCOPE
  the thermodynamic state: e, ke, eTotal, rhoE, h, hTotal, and their identities
  primitive <-> conserved conversion, and temperature recovery through P15's exact inverse
  a CONSERVATIVE total-energy equation in flux form, with an explicit conservative time advance
  a balance diagnostic decomposing every term and the imbalance
  conduction reusing P15's k and its boundary handling
  pressure work, body-force power, a volumetric source
  viscous work TO THE EXTENT P17.4's stress model supports it

OUT OF SCOPE, and NOT to be claimed
  an implicitly coupled rhoE solve inside CompressibleSIMPLE's outer loop. hTotal is a nonlinear
  function of the unknown rhoE through T, so an implicit form needs a Newton linearisation; that
  is solver architecture and belongs with P17.8, not here.
  repairing the production thermal path's cp*T transport (audit.md 3a). It would move every
  variable-cp result in the qualified baseline.
  HLLC or any Riemann flux. Explicitly forbidden.
  3D. The compressible path is 2D-only (requireTwoDimensional); every claim here is a 2D claim.
```

## 1. Measured baseline — the dry-run record these thresholds come from

```text
quantity                                        measured on the UNCHANGED tree
-----------------------------------------------------------------------------------------
h - cp*T, zero reference                        0            exactly
h - cp*T, T_ref = 298.15                        299640.75    = cp * T_ref
(h - e) - R*T, defining e = h - R*T             0            exactly, BOTH references
e - cv*T, zero reference                        0            exactly
e - cv*T, T_ref = 298.15                        299640.75
de/dT vs cv, central difference at 800 K        3.8e-08      the stencil's truncation error
T(h(T)) round trip                              4.5474735088646412e-13 K
  P15's documented bound                        1e-12 K
rho*hTotal - (rhoE + p)                         0            exactly, 4 states
rhoE round trip, relative                       4.8644853019923305e-16  = 2.19 eps
p(gamma) - rho R T, zero reference              -2.91e-11    = 1.7e-16 rel = 0.76 eps
p(gamma) - rho R T, T_ref = 298.15              -143762      = 83 PERCENT
cp(T)*T vs h(T), cp = 900 + 0.15T               +103.2%, +39.5%, +26.3%
e recovery error, Mach 0.0003 .. 0.86           0            exactly, at every speed
```

## 2. Group T — the thermodynamic state  (DETECTION)

| id | criterion | threshold, and why that number |
| --- | --- | --- |
| T1 | `ke = 0.5\|U\|^2` for multiple components | **exactly 0** — a sum of two products, no cancellation |
| T2 | `eTotal = e + ke` | **exactly 0** — one addition |
| T3 | `rhoE = rho * eTotal` | **exactly 0** — one product |
| T4 | `hTotal = h + ke` | **exactly 0** |
| T5 | `(h - e) = R*T` | **exactly 0** — algebraic by the `e := h - R T` definition; measured 0 at both references |
| T6 | `h = e + p/rho` | `<= 4 eps * \|h\|` — passes through a division, so not algebraically exact |
| T7 | `rho*hTotal = rhoE + p` | `<= 4 eps * \|rho hTotal\|` — measured **0**, but the production code may sum in a different order, so the bound is the dimensional floor rather than the one number the probe happened to produce |
| T8 | `e = cv*T` and `h = cp*T` | **exactly 0**, asserted ONLY under the zero reference, and the test must SKIP-BY-CONSTRUCTION elsewhere rather than loosen |
| T9 | Under a NONZERO reference, T8's shortcuts are measurably wrong | `>= 1e3` — the offset is `cp*T_ref = 299640.75`, so this cannot pass by accident |

## 3. Group C — conversion and recovery  (DETECTION)

| id | criterion | threshold |
| --- | --- | --- |
| C1 | primitive → conserved, against a hand-computed `rhoE` | **exactly 0** for the zero reference |
| C2 | conserved → primitive: `U`, then `ke`, then `e = rhoE/rho - ke` | `<= 8 eps * \|e\|` |
| C3 | temperature recovery `T(e)` | `<= 1e-12 K`, **inherited verbatim from P15**, measured 4.55e-13 |
| C4 | pressure recovery `p = rho R T` through P17.2's EOS, not a private formula | **exactly 0** vs `IdealGasEOS::pressure` |
| C5 | round trip primitive → conserved → primitive | `<= 1e-12 K` on T, `<= 8 eps` relative on the rest |
| C6 | round trip conserved → primitive → conserved | `<= 8 eps` relative — measured 2.19 eps |
| C7 | `p = (gamma-1)(rhoE - 0.5 rho\|U\|^2)` agrees with `rho R T` | `<= 4 eps` relative, **zero reference only**; measured 0.76 eps |

## 4. Group E — the conservative equation  (DETECTION)

| id | criterion | threshold |
| --- | --- | --- |
| E1 | The transient term is `d(rhoE)/dt` with `rho_old`/`rho_new` kept SEPARATE, not `rho d(eTotal)/dt` | the two forms must differ by `> 1` on a case where BOTH rho and eTotal change |
| E2 | Convective flux uses the **P17.3 mass flux passed in**, not a recomputed one | structural: one `SurfaceField` argument |
| E3 | `(rhoE + p)U` and `m_dot_f * hTotal_f` agree | `<= 8 eps` relative |
| E4 | Pressure work is PRESENT: dropping `p` from the flux changes the answer | `> 1e-6` relative on a case with non-uniform p |
| E5 | Conduction: `-div(q) = div(k grad T)`, sign verified against a known linear T | **exactly 0** at deep-interior cells for linear T and constant k |
| E6 | Conduction reuses P15's `k`, not a private correlation | structural: 0 new conductivity correlations |
| E7 | Body-force power `rho f . U` is zero when `U = 0` | **exactly 0** |
| E8 | Volumetric source: `d(rhoE)/dt = S_E` in a closed motionless adiabatic domain | `<= 8 eps * N * \|S_E V dt\|` |
| E9 | Viscous work is present to the extent P17.4's stress supports, and the SHORTFALL is stated | structural + documented |

## 5. Group G — global conservation  (the required gate)

| id | criterion | threshold |
| --- | --- | --- |
| G1 | The balance diagnostic reports EVERY term separately: accumulation, convective+pressure, conductive, viscous, body-force, source, imbalance | structural — no single opaque scalar |
| G2 | Adiabatic closed domain, no sources: total energy is CONSTANT | `<= kAccum * eps * N_steps * N * \|rhoE V\|`, `kAccum = 8` EMPIRICAL |
| G3 | Uniform volumetric heating, `U = 0`: `dE = S_E * V * dt` | same bound form |
| G4 | Uniform thermodynamic state: imbalance at round-off | same bound form |
| G5 | Interior face fluxes telescope: an internal face contributes equal and opposite to its two cells | **exactly 0** global sum on a closed box, per-cell values `>> 0` for non-vacuity |

## 6. Group V — invalid states  (DETECTION, each must THROW)

| id | criterion |
| --- | --- |
| V1 | `rho <= 0` |
| V2 | `rhoE` non-finite |
| V3 | `ke > eTotal`, i.e. the recovered `e` is not a valid internal energy |
| V4 | recovered `T` outside the property range — an ERROR, never clamped |
| V5 | non-finite conductivity, non-finite source |
| V6 | NOTHING is clamped and NO energy floor is introduced |

## 7. Group P — preservation

| id | criterion |
| --- | --- |
| P1 | P15 thermal suite unchanged: **161** |
| P2 | Compressible suite: **176** at the P17.4 baseline, plus new |
| P3 | P14 **68**, P16 **315** |
| P4 | The P13-001 SIMPLE fingerprint is BITWISE identical, empty diff |
| P5 | Release+CUDA **2608 registered / 2546 executed**, plus new; 0 failed |
| P6 | CPU-only **2406 / 2346**, plus new; 0 failed |
| P7 | `clang-format-18` clean on every changed file |
| P8 | P15's `EnergyEquation`, `Enthalpy`, `ThermalProperties` are **UNTOUCHED** — verified by SHA-256 against the freeze, NOT by `git diff HEAD` |
| P9 | P17.2 `IdealGasEOS`/`ThermodynamicProperties` and P17.3 `CompressibleMassFlux` UNTOUCHED, by SHA-256 |
| P10 | Static thermal limit: with `U = 0` the total-energy conduction term matches P15's conduction |

```text
P8/P9 say SHA-256 ON PURPOSE. P17.4's L5 used `git diff --numstat HEAD` and reported a file
TOUCHED that the phase never opened, because nothing since P12 is committed. That probe is
banned here.
```

## 8. Group D — determinism

| id | criterion | threshold |
| --- | --- | --- |
| D1 | Repeated conversion and assembly are **bitwise** identical | `==` on every entry |

## 9. Group X — CPU/GPU equivalence

| id | criterion |
| --- | --- |
| X1 | The GPU compressible/energy surface is probed and REPORTED, not assumed |

```text
X1 IS EXPECTED TO BE "NOT PRESENT", NOT "PASS".
The audit measured 0 hits for compressible, IdealGas, EquationOfState, ThermodynamicProperties,
Enthalpy, rhoE, totalEnergy and machNumber across cuda/ and include/cfd/gpu/. A GPU total-energy
path would require, in order: a GPU EOS, a GPU compressible mass flux, a GPU compressible momentum
assembly and a GPU compressible solver. None exists. This is recorded as blocked by absence, with
the probe output as the evidence -- never as PASS, and no path is to be invented to tick the box.
```

## 10. Group N — non-vacuity controls  (each MUST fail)

| id | planted defect | must break |
| --- | --- | --- |
| N1 | `e = h + R T` (sign flip on the defining relation) | T5, T6, T7 |
| N2 | `ke = \|U\|^2` (drop the 0.5) | T1, T2, C5 |
| N3 | Drop `p` from the convective flux, leaving `rhoE * U` | E4 |
| N4 | Flip the sign of the conductive term | E5, G2 |
| N5 | Transient uses `rho_new` for both old and new | E1 |
| N6 | `rhoE = rho * e` (omit `ke`) | T3, C1, C5 |
| N7 | Accept `ke > eTotal` instead of throwing | V3 |
| N8 | Use `cp(T)*T` for `h` instead of `SensibleEnthalpy` | T8 under `cp(T)`; must be caught because the dry-run measured a 26-103% error |

```text
Each control's PROBE SET is derived from the criterion it targets, and is stated as the full
momentum-and-energy suite rather than a hand-picked regex. P17.4's N6 was declared vacuous only
because its fail-regex excluded the file containing an existing detector -- a too-narrow probe,
not a coverage gap. When a control here comes back vacuous, the probe set is suspected FIRST.
```

## 11. The dual-`cp` decision is itself gated

| id | criterion |
| --- | --- |
| Z1 | The energy path never reads `ThermodynamicProperties::specificHeatPressure()` — structural, 0 call sites |
| Z2 | Constructing the energy state with an enthalpy model whose `cp` disagrees with the EOS block's `cp` **THROWS** |
| Z3 | Z2's tolerance is derived, not guessed, and a consistent pair constructs cleanly |

## 12. Gate verdict

P17.5 closes only when **T, C, E, G, V, P, D, Z all pass, every N control fails, and X1 is
reported as measured**.

```text
FORBIDDEN
  using p = (gamma-1)(rhoE - ke) as the PRESSURE PATH -- measured 83% wrong under a nonzero
    reference. It is a verification identity only, gated on the zero reference.
  transporting cp*T in the new energy flux
  redefining P15's enthalpy or internal-energy zero points
  introducing an energy floor or clamp to make a test pass
  claiming X1 as PASS
  claiming an implicitly coupled rhoE solve
  `git diff --numstat HEAD` as an "untouched" predicate
  implementing anything in P17.6 - P17.10, and HLLC in particular
```

## 13. THE COVERAGE MAP — every criterion, and the test that measures it

Filled in at implementation time. **A criterion with no test named here is NOT verified**, whatever
the suite total says. This table is the deliverable that P17.4 lacked.

```text
criterion  test that measures it                                          status
-------------------------------------------------------------------------------------
T1..T9     TotalEnergyStateTest.*                                         TO BE FILLED
C1..C7     TotalEnergyConversionTest.*                                    TO BE FILLED
E1..E9     TotalEnergyEquationTest.*                                      TO BE FILLED
G1..G5     TotalEnergyConservationTest.*                                  TO BE FILLED
V1..V6     TotalEnergyInvalidStateTest.*                                  TO BE FILLED
P1..P10    the regression harness + SHA-256 checks                        TO BE FILLED
D1         TotalEnergyStateTest.RepeatedEvaluationIsBitwiseIdentical      TO BE FILLED
X1         tools/gpu_probe.sh                                             TO BE FILLED
Z1..Z3     TotalEnergyContractTest.*                                      TO BE FILLED
```
