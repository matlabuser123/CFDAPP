# P17.2 — Ideal-gas EOS inverse: summary

**P17.2 → PASS.** 2026-09-26.

One authorized interface addition: `pressure(rho, T)` on the `EquationOfState`
abstraction, implemented in `IdealGasEOS` as `p = rho * R * T`.

---

## Scorecard

```text
P1  absolute-pressure semantics documented in the interface   PASS
P2  SIMPLE still solves gauge pressure                        PASS, untouched
P3  referencePressure remains the single bridge               PASS, untouched
P4  density(p,T) unchanged                                    PASS, %.17g identical
P5  derivatives unchanged                                     PASS, %.17g identical
R1  (p,T) -> rho -> p                                         PASS  <= 1e-12 rel
R2  (rho,T) -> p -> rho                                       PASS  <= 1e-12 rel
R3  representative non-default states                         PASS  4 states, 3 gases
R4  p = rho*R*T against an independent expectation            PASS
V1  non-positive rho rejected                                 PASS
V2  non-positive / non-finite T rejected                      PASS
V3  overflowing product rejected                              PASS
V4  deterministic                                             PASS  64 repeats, bitwise
V5  message names the field                                   PASS
T1  cp - cv = R preserved                                     PASS  by construction
T2  gamma = cp/cv preserved                                   PASS  by construction
T3  speedOfSound tests preserved                              PASS  7/7
N1..N4 non-vacuity controls                                   ALL DETECTED
G1..G6 preservation                                           PASS, see below
G7  P13-001 SIMPLE fingerprint                                BITWISE IDENTICAL
```

## What was added, and what was not

```text
include/cfd/compressible/EquationOfState.hpp      24 +   the pure virtual + its contract
include/cfd/compressible/IdealGasEOS.hpp           5 +   the override
src/compressible/IdealGasEOS.cpp                  13 +   p = rho*R*T, validated
tests/unit/compressible/test_ideal_gas_eos.cpp   123 +   9 new tests
                                    4 files, 165 insertions, 0 deletions
```

**No second thermodynamic-property implementation was created.**
`EquationOfState`, `IdealGasEOS` and `ThermodynamicProperties` remain the only homes,
as the authorization required.

On the **abstraction**, not the concrete class: `PressureIsReachableThroughTheAbstraction`
holds an `EquationOfState&` and inverts through it, so the energy equation and Riemann
flux to come never need to know or down-cast to a concrete EOS type.

### Type convention

The authorization wrote `virtual double pressure(double rho, double T) const = 0;`.
The implementation uses `Real`, this codebase's typedef for `double` and what every
neighbouring signature in `EquationOfState` uses. Same type, spelled as its
neighbours spell it.

## The bound, and why it is 1e-12

`p/(R*T)` then `rho*R*T` is two roundings of a double, so exact bitwise recovery is
not guaranteed for arbitrary values. 1e-12 relative is ~4500x the double epsilon and
four orders **tighter** than the 1e-9 the existing
`EosConsistencyErrorIsAtFloatingPointTolerance` low-Mach gate already uses. Derived
from the operation before any result was seen, not fitted afterwards.

Control **N4** is what proves the bound is load-bearing: perturbing `R` by 1e-6
relative — six orders above the bound, and invisible to a casual reader — fails R4
and both round trips. A looser tolerance would have absorbed it silently.

## Preservation

```text
EOS + new tests              21 / 21       (12 existing + 9 new)
ThermodynamicProperties       7 / 7
existing compressible suite 129 / 129      the audit's 90 tests and their neighbours
P14 transport                68 / 68
P15 thermal                 161 / 161
P16 turbulence              315 / 315
P13-001 SIMPLE fingerprint  10 cases, BITWISE IDENTICAL
```

The fingerprint is the sharpest of these. An EOS inverse that nothing in the
incompressible path calls has no business moving `SIMPLE`, and it moved not one bit
across 10 cases and 130 lines.

## Non-vacuity controls — each failed for its INTENDED reason

```text
N1  drop R: return rho*T
      PressureMatchesRhoRT, both round trips, abstraction reachability   the R group
N2  return a constant
      R4, both round trips, plus the overflow and linear-scaling shape checks
N3  remove the input validation
      PressureRejectsNonPositiveDensityOrTemperature                      exactly V1/V2
N4  perturb R by 1e-6 relative
      R4, both round trips, abstraction -- the 1e-12 bound caught a 1e-6 error

restore   VERIFIED by SHA-256 after every control; 0 markers left;
          21/21 at baseline afterwards
```

### A design property the controls surfaced

N3 broke **one** test, not two. Removing the input validation still leaves the
*result* check (`requirePositiveFinite(p, "pressure")`), so a non-finite `rho` or `T`
produces a non-finite product that is rejected anyway. The non-finite path is
therefore defended twice; only the non-positive path depends solely on input
validation. Not planned, and not visible without the control.

## Source state

```text
V2 at the P17 baseline   721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955
V2 after P17.2           fb69dc2a9e045f939df71243a01cea931cb7ce29a64e35b2171bc95c9cd284af
```

Authorized delta, attributed: tracked engineering files 112 -> 116, exactly the four
above, 165 insertions and zero deletions. Every other changed file in the tree is
P13-P16's pre-existing uncommitted work, untouched by P17.

## Next, in the authorized order

```text
P17.1  existing variable-density verification
P17.3  existing continuity verification
P17.4  existing momentum verification
P17.6  existing low-Mach verification
        -> P17.5  total-energy equation
        -> P17.7  HLLC / Riemann flux
        -> P17.8  shock validation
```

GPU compressible work is outside the authorized P17 scope and is recorded as future
work, not started.
