# P18.6 — total-energy conservation (§24, §25, §51) — THE PRIMARY G7 MEASUREMENT

```text
G7's acceptance, verbatim:
    "|energy imbalance| / |total energy| <= 8 * eps * N_cells, the same derived floor as G3 and
     for the same reason."

THE THRESHOLD IS INHERITED, NOT CHOSEN. It is G3's floor, for the same reason G3 has it: a sum of
N_cells terms each carrying round-off. Nothing in P18.6 moved it.
```

---

## 1. The conserved quantity, defined explicitly (§24)

```text
    E = sum_cells V_c [ rho_c cp(T_c, Y_c) T_c  +  rho_c sum_i Y_{i,c} h_f,i ]        [J]
                        \___________________/     \_________________________/
                         SENSIBLE                  CHEMICAL

Section 24 requires this to be stated rather than implied, and requires that the sensible part
alone is NOT used: "Do not assess only sensible thermal energy; exothermic chemistry is expected to
move energy between chemical and sensible forms."

THE SENSIBLE TERM IS rho cp T BECAUSE THAT IS WHAT CFDApp TRANSPORTS -- EnergyEquation.hpp:347
says so, and the whole convention follows from it (energy_accounting.md section 1).
```

## 2. Measured (`logs/06_probe.log`)

16 cells, 50 explicit steps, `q = 128`, `dt = 1e-3`, exothermic `A->B`:

| quantity | value |
|---|---:|
| `E_initial` | **32 000 000 J** (sensible 32 000 000, chemical 0) |
| `E_final` | **31 999 999.999999955 J** (sensible 57 599 999.99999997, chemical −25 600 000.000000015) |
| **absolute drift** | **4.470348e-08 J** |
| **relative drift** | **1.396984e-15 = 6.29 eps** |
| worst over the march | 1.513399e-15 = **6.82 eps** |
| **G7 allows** `8·eps·N_cells` | 2.842171e-14 = **128 eps** |
| **margin** | **≈ 19×** |

```text
G7 PASSES, with a 19x margin -- and the margin is worth stating because P18.5's G6 margin was only
3x. These are different criteria with differently derived floors; the comparison is not a quality
ranking, but it does say that tightening G7 later would have room where tightening G6 would not.
```

## 3. The two pools moved oppositely, by the same amount

```text
    d(sensible) = +2.560000e+07 J
    d(chemical) = -2.560000e+07 J
    sum         =  4.47e-08 J, i.e. 6.29 eps relative

THIS IS THE RESULT THAT MATTERS, and it is why reporting only the sensible pool would have been
wrong. The sensible energy rose by 80% of the initial total -- an exothermic reaction converting
40% of A. Had the test asserted "thermal energy is conserved" it would have FAILED on correct
physics; had it asserted nothing about the chemical pool it would have passed for an
implementation that created energy from nothing.

T: 1000 -> 1800.000000 K        Y_A: 1 -> 0.600000

and 40% conversion x 2000 K per full conversion = 800 K, which is exactly the rise observed. The
conservation result and the analytic expectation agree independently.
```

## 4. Non-vacuity — the chemistry actually ran

```text
A CONSERVATION TEST THAT CONSERVES BECAUSE NOTHING HAPPENED IS VACUOUS, and this is the project's
most-repeated failure mode. The test asserts, before the drift bound:

    sensibleRose > 0                              the sensible pool MUST rise
    state.temperature[0] > t0 + 100.0             T rose by at least 100 K  (measured: 800 K)
    state.composition.massFraction(0,0) < 0.99    A was consumed            (measured: 0.60)

THESE GUARDS HAVE ALREADY FIRED ONCE IN ANGER. When the march was first shortened to 60 steps at
q = 0.5 to stay inside P18.1's tolerance, T rose only 3.75 K and Y_A fell to 0.998 -- and the
guards failed the test rather than letting a meaningless "conservation" result through. The fix was
to raise the RATE, not to relax the guards. See closed_reactor.md section 4.
```

## 5. Per-step accounting (§25)

```text
In the closed adiabatic reactor every section 25 term other than the two pools is IDENTICALLY
ZERO BY CONFIGURATION, not by cancellation:

    boundary energy flux   = 0    adiabatic, closed
    other source work      = 0    U = 0
    pressure work          = 0    the P15 thermal equation has no p div(U) term at all

so the balance is  Delta E_thermal + Delta E_chemical = 0, measured pool by pool in section 3, and
the one-step form Delta u_sens = dt S_E measured at closed_reactor.md section 2 to 0.51 eps.

THE INDIVIDUAL TERMS ARE PRESERVED in logs/06_probe.log rather than only their sum, so a future
phase can see which pool moved rather than only that the total held.
```

## 6. What G7 does and does not establish here

```text
ESTABLISHES
  - the chemical and sensible pools exchange energy with a closure of 6.29 eps relative, against
    an inherited allowance of 128 eps, over 50 steps on 16 cells;
  - the final temperature matches an INDEPENDENT analytic enthalpy balance BITWISE at two
    conversion fractions (closed_reactor.md section 3);
  - the heat released equals the enthalpy change implied by the species consumed -- G7's own
    cross-check -- measured as the molar/mass route agreement at EXACTLY 0 eps
    (heat_source.md section 2).

DOES NOT ESTABLISH
  - conservation under flow. U = 0 throughout; convective and diffusive energy transport are
    P18.2's and P15's and are not exercised by this test.
  - conservation over a long march. The sum-to-one budget bounds the march at ~72 steps, measured
    and recorded (closed_reactor.md section 4). The ENERGY drift is exactly 0 for 1000+ steps, so
    this is a limit on P18.1's invariant, not on G7.
  - anything about the compressible total-energy path, which still refuses a variable cp and is
    untouched (audit.md section 1).
  - G8, G9 or G11. Section 51 forbids claiming them and none is claimed.
```
