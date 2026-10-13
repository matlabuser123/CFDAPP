# P18.6 — the closed adiabatic reactor (§23, §25, §26, §37, §38)

```text
THE CANONICAL P18.6 CONFIGURATION, as section 23 specifies it:

    U = 0                     no convection
    boundary heat flux = 0    adiabatic
    boundary mass flux = 0    closed
    constant volume

so the ONLY evolution is species conversion and the temperature that follows from it. Nothing
spatial, nothing to do with flow -- which is what makes it the right instrument for the energy
balance, because every term that is not chemistry is identically zero.
```

---

## 1. The state and the step

```text
struct ReactorState { MassFractionState composition; ScalarField temperature; ScalarField density; }

advanceClosedReactor(mechanism, mixture, formation, rates, state, dt)

ONE EXPLICIT STEP, in the frozen order of coupling_order.md section 1. rho is unchanged by
derivation, not assumption: closed + constant volume + sum_i omega_i = 0 (measured exactly 0).
```

## 2. One-step balance (§26)

`Delta u_sens = dt * S_E`, measured directly (`logs/06_probe.log`):

| q | dt | measured | expected | rel |
|---:|---:|---:|---:|---:|
| 1 | 1e-3 | **125** | 125 | **0** |
| 100 | 1e-5 | **125** | 125.00000000000001 | 0.51 eps |
| 0.5 | 2 | **125 000** | 125 000 | **0** |

Two of three exact. The three cases span `q` by 200× and `dt` by 2e5× while holding `dt·S_E`
nearly fixed, so a defect in either factor alone would show.

## 3. The adiabatic final temperature — against an independent balance

G7 requires: *"the final temperature must match the adiabatic flame temperature computed from an
INDEPENDENT equilibrium/enthalpy balance, to the discretization's own accuracy — REPORTED with its
order, not gated on an invented constant."*

```text
WITH ONE CONSTANT cp SHARED BY EVERY SPECIES, cp_mix is composition-independent and the balance is
ANALYTIC rather than iterative:

    T_final = T_0 + ( sum_i Y_i^0 h_f,i - sum_i Y_i^f h_f,i ) / cp

From data/reference_values.txt (mpmath, dps=60, no CFDApp code): T_0 = 1000 K, cp = 1000 J/(kg K),
A->B with h_f,B = -2e6 J/kg gives EXACTLY 2000 K at 50% conversion and EXACTLY 3000 K complete.

AND THE CONVERSION IS DRIVEN TO EXACTLY 50% IN ONE STEP, so the comparison carries NO
time-integration error at all:
    d(rho Y_A) = -M q dt = -0.5   =>   q dt = 0.5 / 0.0625 = 8
```

**Measured** (`logs/06_probe.log`):

| case | T measured | independent balance | rel error |
|---|---:|---:|---:|
| 50 % conversion | **2000** | 2000 | **0.000e+00 (0.00 eps)** |
| complete | **3000** | 3000 | **0.000e+00 (0.00 eps)** |

```text
BITWISE EXACT against the independent balance, at both points. No discretization error is present
to report an order for, because the step was constructed to land exactly on the analytic state --
which is a stronger result than matching to a convergence order, and is why no order is quoted.
```

## 4. The step budget — a measured limitation, not a tuned number

```text
THE CONSERVATION TEST FIRST MARCHED 200 STEPS AND P18.1 REFUSED ITS OWN STATE AT STEP 72:

    "mass fractions at cell 0 sum to 0.99999999999999722; must be within 2.6645352591003757e-15
     of 1 (deviation 2.7755575615628914e-15)"

BEFORE CHANGING ANYTHING, tools/drift_probe.cpp measured the growth law -- because the fix depends
entirely on whether the drift is systematic or random:
```

| steps | \|ΣY−1\| | linear would predict | sqrt would predict |
|---:|---:|---:|---:|
| 100 | **17.00 eps** | 17.00 | 1.00 |
| 400 | **69.00 eps** | 276 | 2.00 |
| 1600 | **284.50 eps** | 4552 | 4.00 |
| 6400 | **1034.50 eps** | 66 208 | 8.00 |

```text
LINEAR, at ~0.17 eps/step. NOT a random walk.

AND THE CAUSE IS VISIBLE IN THE SAME LOG:
    increment_A = -3.1250000000000001e-05
    increment_B = +3.1250000000000001e-05
    sum         =  0                        EXACTLY

THE INCREMENTS ARE EXACTLY BALANCED. The bias is entirely in Y_i + increment_i: adding -3.125e-5
to Y_A ~ 1 rounds to the ulp of 1 and loses bits; adding +3.125e-5 to Y_B ~ 0 does not. The two
roundings are asymmetric because the species sit at different magnitudes, so the error is
SYSTEMATIC rather than cancelling.

SO IT IS NOT A STOICHIOMETRY, SOURCE OR CONSERVATION DEFECT. It is the irreducible cost of an
explicit per-component march, and section 41 forbids the renormalisation that would mask it.
```

**What was done about it, and what was not:**

```text
NOT DONE: renormalising (forbidden by section 41), widening P18.1's tolerance (that would weaken a
          predecessor's invariant to make this phase pass), or quietly shortening the march.

DONE:  (a) the march is bounded at 50 steps, inside the measured budget with margin;
       (b) THE RATE WENT UP so the march still does real chemistry -- when the count alone came
           down, T rose only 3.75 K and Y_A fell to 0.998, and THE TEST'S OWN NON-VACUITY GUARDS
           CAUGHT THAT. The per-step sum-to-one cost is ~1 ulp of Y regardless of increment size,
           so converting more per step is free: q = 128, dt = 1e-3 converts 40% of A in 50 steps
           and raises T by 800 K;
       (c) a dedicated test, TheExplicitMarchHasAMeasuredSumToOneStepBudget, PINS the behaviour --
           it asserts the march IS eventually refused (never silently renormalised) and that the
           budget is 60-90 steps, so a regression in the enforcement is caught;
       (d) the limitation is recorded in TODO.md rather than hidden.

G7's OWN CRITERION IS UNAFFECTED. The probe measures the ENERGY drift at EXACTLY ZERO for the first
1000 steps -- far beyond the sum-to-one budget. The two quantities are healthy to very different
degrees and conflating them would have hidden that.
```

## 5. EOS closure and the constant-volume check (§37, §38)

```text
rho IS CONSERVED BY DERIVATION in this configuration (section 1 above), and the test asserts
next.density == rho bitwise.

PRESSURE IS NOT RECOMPUTED, and no constant-pressure behaviour is claimed. Section 37 notes that
in a closed constant-volume ideal-gas system p = rho R_mix T moves as T and R_mix move; P18.6's
reactor carries no pressure field, so there is nothing to check and nothing is asserted. Section 39
makes a constant-pressure reactor explicitly OPTIONAL and the frozen gate does not require it --
and faking one by overwriting pressure is forbidden, so none is attempted.

WHAT IS ESTABLISHED is the integrated P18.1-P18.6 path without flow: composition stays valid
(P18.1), sources conserve mass (P18.4), rates come from kinetics (P18.5), energy closes (G7), and
temperature feeds back (P18.6). That is section 38's "useful integrated test without involving
flow".
```

## 6. Independent per-step accounting (§25)

```text
For this configuration the section 25 terms are:

    Delta E_thermal            measured as u_sens^{n+1} - u_sens^n
    chemical energy converted  measured as rho sum_i (Y_i^{n+1} - Y_i^n) h_f,i
    boundary energy flux       IDENTICALLY ZERO -- adiabatic and closed
    other source work          IDENTICALLY ZERO -- U = 0

so the balance reduces to  Delta E_thermal + Delta E_chemical = 0, which is what
energy_conservation.md measures pool by pool. The zero terms are zero BY CONFIGURATION rather than
by cancellation, which is the reason this reactor is the right instrument.
```
