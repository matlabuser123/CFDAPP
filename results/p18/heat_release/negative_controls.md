# P18.6 — negative controls (§49)

```text
ALL TWELVE ARE PERMANENT TESTS, not source mutations. Every defect section 49 names is expressible
either through the production API or as an alternative arithmetic expression evaluated alongside
the correct one, so each control runs in every regression and cannot rot.

EACH REPORTS ITS MARGIN, so "detected" is quantified rather than asserted.

THREE OF THE TWELVE ARE G7's OWN CONTROLS, written into the frozen gate before this phase opened:
N1 (heat-release sign), N8 (the rate fed to species but not energy) and N2 (h_f replaced by the
sensible reference enthalpy).
```

---

## 1. The twelve

| # | Defect | Margin measured | Result |
|---|---|---|---|
| **N1** | flip the reaction-heat sign | **2×** the magnitude, and the sign inverts | **DETECTED** |
| **N2** | omit formation enthalpy / use the sensible reference | **> 1.8e4×** | **DETECTED** |
| **N3** | stale temperature for the next kinetics evaluation | rate differs **> 0.1 %** | **DETECTED** |
| **N4** | stale composition for temperature recovery | T differs **> 10 %** | **DETECTED** |
| **N5** | double-count chemical energy | **9.6 %** of `Delta H_r` | **DETECTED** |
| **N6** | omit the reaction energy source | `S_E` and `omega_i` both **exactly 0** | **DETECTED** |
| **N7** | wrong species enthalpy | exothermic → **endothermic** (sign flip) | **DETECTED** |
| **N8** | wrong progress rate for energy | **150 %** gap, caught by the route cross-check | **DETECTED** |
| **N9** | mol/kmol factor error | **exactly 1000×** | **DETECTED** |
| **N10** | break the non-reacting thermal identity | **0 bitwise differences** required; defect gave ~1 ulp/step | **DETECTED** |
| **N11** | omit one reaction from the total `S_E` | **> 50 %** of the magnitude | **DETECTED** |
| **N12** | allow non-finite / negative temperature | refused, 5 values × 3 entry points | **DETECTED** |

## 2. N1 — G7's own first control

G7: *"heat-release sign flipped (exothermic ↔ endothermic) must FAIL."*

```text
    correct   S_E = +250000 W/m^3   at q = 2, Delta H_r^0 = -125000
    flipped   S_E = -250000         the gap is 2x the magnitude -- no tolerance makes this ambiguous

AND IT IS CHECKED THROUGH THE PHYSICS, not only the arithmetic: the exothermic and endothermic
reactions must give OPPOSITE signs (EXPECT_LT(endothermic * exothermic, 0.0)). A GLOBAL sign flip
preserves that relation, so the test asserts both the magnitude and the cross-reaction sign
structure -- the second catches a per-reaction error the first cannot.
```

## 3. N2 — G7's own third control, and it names the trap

G7: *"h_f,i replaced by the SENSIBLE reference enthalpy must FAIL. This control matters because
those are different quantities that the existing API could easily conflate."*

```text
    Delta H_r^0 from FORMATION enthalpies  = -125000 J/mol
    from the SENSIBLE references (111/222/333 J/kg) = 6.9375 J/mol
    ratio                                   > 1.8e4x

AND THE COMPLEMENTARY ASSERTION IS STRONGER: Delta H_r^0 is BITWISE IDENTICAL whether the sensible
references are 111/222/333 or 0/0/0. That is the formal statement that a sensible offset is
unobservable -- so the two quantities are not merely different in value, they have different
observational status.

STRUCTURALLY, the confusion is hard to write: h_f lives in its own type with its own name
(species_enthalpy.md section 2), rather than beside `referenceEnthalpy` on one class.
```

## 4. N5 — the primary design gate's control

```text
THE DEFECT: build the source from the TOTAL species enthalpy h_f,i + h_i(T) instead of h_f,i alone.
The extra term is the sensible enthalpy of the species converted, which the thermal equation
ALREADY evolves (energy_accounting.md section 5).

WITH EQUAL cp_i THE SPURIOUS TERM CANCELS EXACTLY, so an equal-cp mixture CANNOT DETECT THIS
DEFECT AT ALL -- and the test says so, asserting the two forms are bitwise equal there. That is the
same vacuity trap as N4, stated rather than stumbled into.

WITH UNEQUAL cp_i, measured at T = 1500 K:
    Delta H_r^0 (correct, the source)  = -125000.00000000000
    Delta H_r(T) (the defect)          = -136990.70721875000
    relative gap                       = 9.6 %

9.6% IS THE DANGEROUS SIZE: large enough to matter physically, small enough to be mistaken for a
discretization error. A PASS/FAIL conservation test would hide it; the quantitative one does not.

AND THE PRODUCTION SOURCE IS ASSERTED TO USE THE FORMATION-ONLY FORM:
    EXPECT_DOUBLE_EQ(source, -correctU);   EXPECT_NE(source, -wrongU);
so the control checks the claim, not just the arithmetic difference.
```

## 5. N4 — and why it needs an unequal `cp` to exist at all

```text
THE DEFECT: recover T from the stale composition.

WITH EQUAL cp_i THERE IS NOTHING TO DETECT -- cp_mix is composition-independent, so stale Y gives
the bitwise same T. A control built on the equal-cp fixture would report DETECTED-by-nothing.

So the control uses cp_B = 2500 against cp_A = cp_C = 1000, and ASSERTS THE CONFOUND IS PRESENT:
    cp_mix before = 1000.0      (pure A)
    cp_mix after  = 1750.0      (50% B)
    EXPECT_NE(cpBefore, cpAfter) << "without this, N4 would be vacuous";

then measures the defect: the stale-composition recovery differs by > 10%.
```

## 6. N8 — the coupling control, and the route cross-check that detects it

G7: *"the reaction rate fed to species but NOT to energy must FAIL — this is the control that the
two are actually coupled rather than separately plausible."*

```text
    species at q = 2, energy at q = 5
    coupled    S_E = 250000, and the molar/mass routes agree to 0 eps
    mismatched S_E = 625000, and the routes disagree by 150%

THE DETECTOR IS THE CROSS-CHECK, not either route. The molar route takes q; the mass route takes
omega. Each is individually plausible at any rate -- only comparing them reveals that the two
halves of the step disagree. That is why both routes are implemented rather than one
(heat_source.md section 7).

IN PRODUCTION THE MISMATCH IS UNREPRESENTABLE: the rates are a single argument shared by steps 3
and 4 of the frozen order, so there is no code path in which species and energy see different
values (coupling_order.md section 2).
```

## 7. N10 — and it caught a real defect

```text
THE ONLY CONTROL IN THIS SET THAT FAILED AGAINST THE FIRST IMPLEMENTATION. With exactly zero rates
the state drifted ~1 ulp per step in both Y and T. Two causes, both fixed, both recorded:
nonreacting_limit.md sections 1-2. Preserved at logs/03_four_failures_PRESERVED.log.

NOW: 0 bitwise differences over 10 zero-rate steps on 32 non-uniform cells -- max |dT| = 0 K,
max |dY| = 0, every field, plus bitwise-equal total/sensible/chemical energy.

AND THE CONTROL CARRIES ITS OWN NON-VACUITY GUARD: the same state with a non-zero rate is asserted
to MOVE, so a stepper that does nothing at all cannot pass.
```

## 8. N6, N7, N9, N11, N12 — the remaining five

```text
N6  q = 0 gives omega_i = 0 and S_E = 0 EXACTLY, 8 cells x 2 reactions x 3 species. The algebraic
    half of the backward-compatibility claim; N10 is the behavioural half.

N7  permute which species carries which h_f: h_f,A and h_f,B swapped turns S_E from +125000 to
    -125000 -- an exothermic reaction becomes ENDOTHERMIC. A sign change, not a small error.

N9  h_f entered per gram instead of per kilogram: exactly 1000x, and exact because the weights are
    dyadic. Section 43's required control.

N11 drop one reaction from a two-reaction mechanism: 156250 becomes 250000, a gap of more than
    half the correct magnitude. The omitted reaction was the ENDOTHERMIC one, so its absence also
    changes the net sign balance -- harder to miss than dropping a same-sign term.

N12 non-finite and non-positive temperature and energy: refused at 5 values x 3 entry points, plus
    invalid density. The message is asserted to contain "No temperature floor" so the refusal
    cannot be mistaken for a clamp. See invalid_states.md section 5.
```

## 9. Instrument non-vacuity, beyond the twelve

```text
THE DRIFT PROBE (tools/drift_probe.cpp) is instrumentation, so it carries its own discrimination
check rather than a pass/fail: it reports the measured drift against BOTH a linear and a sqrt
prediction, so the growth law is read off the data rather than assumed. The answer was linear --
17/69/284/1034 eps at 100/400/1600/6400 steps -- which is what made the limitation diagnosable
instead of merely observable.

THE REFERENCE SCRIPT'S OWN VACUITY was caught before it reached a test: its molar-shift invariance
check reported "invariant: True" for a 1:1 reaction, where sum_i nu_i = 0 makes the shift cancel
for the WRONG reason. Fixed with a mechanism that conserves mass but not moles, where the molar
shift is asserted NOT to cancel (reaction_enthalpy.md section 6).

TWO TEST-INSTRUMENT ERRORS were preserved rather than quietly corrected:
  logs/02_first_failure_PRESERVED.log  a negative q fed to an IRREVERSIBLE reaction; P18.4's guard
                                       refused it, correctly.
  logs/03_four_failures_PRESERVED.log  an endothermic dt that would consume 1.8x the available A;
                                       the production guard refused it, correctly.
In both cases the PRODUCTION CODE WAS RIGHT and the test was wrong, which is worth recording as
clearly as the reverse.
```
