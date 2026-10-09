# P18.5 — audit (§1) and the gate it closes (§41)

```text
WRITTEN BEFORE ANY CODE CHANGE.
GATE            results/p18/gates/P18_GATES.md, sha256 re-verified at P18.5 open:
                921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3  UNCHANGED
P18.5 START     P18_START_SOURCE_FINGERPRINT
                8f2b5f874dfe2c3d652de546d034d8f5aa24f9996d86c1df0db76a2be7d61bc2
EVIDENCE PATH   G6 names results/p18/reactions/; the authorization §49 names
                results/p18/arrhenius_chemistry/. The authorization is later and more specific, so
                evidence lives there. The frozen gate is NOT edited to match.
```

---

## 1. G6 — what P18.4 left, and what P18.5 must deliver

P18.4 closed G6 criterion (b), the stoichiometric closure, and said so explicitly rather than
claiming the gate. P18.5 is the phase G6's own `phase` field names.

```text
G6 acceptance, criterion (a) -- THIS PHASE:
    "relative error <= 32 * eps * scale on each rate.
     DERIVED: an Arrhenius rate is exp(-Ea/RT) times a power law -- a handful of transcendental
     evaluations, each correctly rounded, so tens of eps is the achievable floor. 32 eps is
     P17.7's own frozen multiple for the same kind of pointwise closed-form comparison, inherited
     rather than invented."

G6 measured:
    "omega_i for a known mechanism against an INDEPENDENT arbitrary-precision implementation
     (Python/mpmath, no CFDApp code), over a matrix of T, p and composition spanning the intended
     range."

G6 negative controls -- the two kinetics ones are P18.5's:
    * Arrhenius exponent sign flipped must FAIL
    * activation energy set to 0 must FAIL (removes the T dependence entirely)
    (the third, a perturbed stoichiometric coefficient, was closed by P18.4)

G6 evidence:
    "results/p18/reactions/ + the independent reference script preserved under tools/"
```

```text
SO P18.5 MUST, to close G6:
  1. compare omega_i -- not just k -- against an INDEPENDENT mpmath implementation;
  2. do so over a MATRIX of T, p and composition;
  3. meet 32 * eps RELATIVE on each rate;
  4. PRESERVE the reference script under tools/;
  5. make both kinetics controls detect.
mpmath 1.3.0 is available in the WSL environment, so the reference G6 asks for by name is
buildable rather than approximated.
```

```text
ONE READING THAT NEEDS CARE, recorded now so it is not resolved sloppily later.
G6's second control says "activation energy set to 0 must FAIL (removes the T dependence
entirely)", while the authorization §9 says Ea = 0 reduces to k = A T^n and should be verified
ANALYTICALLY. These are not in conflict:
  * Ea = 0 IS a valid analytical case, and §9's test asserts k = A T^n there;
  * setting Ea = 0 in a mechanism whose rate comparison DEPENDS on the exponential term breaks
    that comparison, which is the control.
Both are implemented, and they assert different things about different mechanisms.
```

## 2. Requirement classification (§1)

| P18.5 requirement | Class | Basis |
|---|---|---|
| universal gas constant `R_u` | **DIRECT REUSE** | P18.3's `kUniversalGasConstant` = 8.31446261815324 J/(mol·K). One source of truth. |
| molecular weights `M_i` | **DIRECT REUSE** | P18.3's `SpeciesThermodynamics::molecularWeight()`. P18.5 stores none. |
| composition `Y_i`, density `rho` | **DIRECT REUSE** | P18.1's `MassFractionState` and a density field |
| reaction topology, `nu'_i` | **DIRECT REUSE** | P18.4's `Reaction::reactantCoefficient()` |
| `q_r` -> `omega_i` | **DIRECT REUSE** | P18.4's `reactionSpeciesSources()`. P18.5 computes **no** `omega_i` of its own (§25) |
| mass conservation | **DIRECT REUSE** | P18.4's `sourceConservation()`. The formula is **not** duplicated (§26) |
| progress-rate container | **DIRECT REUSE** | P18.4's `ReactionProgressRates` — P18.5 fills it |
| `A`, `n`, `Ea` | **MISSING** | **0** case-sensitive occurrences of `Arrhenius`, `activationEnergy`, `preExponential`, `rateConstant`, or a `Kinetics`/`RateCoefficient` class anywhere |
| `k(T)` | **MISSING** | nothing |
| mass-action `q_f` | **MISSING** | nothing |
| concentration `C_i` | **MISSING** | nothing |

```text
THE SEARCH WAS CASE-SENSITIVE. A case-insensitive grep for "arrhenius" returns hits -- all of them
P18.4's own COMMENTS saying P18.5 owns it ("Nothing here knows about A, n, Ea or exp()" and
"P18.5: state + Arrhenius -> q"). Those were recorded in P18.4's integrity log for exactly this
reason, and they are the only ones.
```

## 3. The architecture boundary P18.4 built, and P18.5 must respect

```text
P18.4 deliberately knows nothing about kinetics, and P18.5 must add none of P18.4's algebra:

    P18.5                        P18.4                       P18.2
    -----                        -----                       -----
    T, rho, Y_i, A, n, Ea
      -> k(T)
      -> C_i = rho Y_i / M_i
      -> q_r = k Pi C_i^(nu'_i)
                                 -> omega_i = sum_r M_i nu_i q_r
                                                             -> Su, conservative transport

P18.5 FILLS ReactionProgressRates. It does not touch omega_i, stoichiometry, or the source
assembly -- §25 forbids bypassing P18.4, and N9 is the control for it.
```

## 4. Findings recorded, not acted on

```text
F1  THE PATANKAR Sp IS STILL ZERO. P18.4 left it so because linearising omega in Y needs
    d(omega)/d(Y), and P18.5 now HAS the kinetics to differentiate. But §30 says not to expand the
    chemistry integrator, and an implicit source split is an integrator concern, not a rate one.
    P18.5 therefore provides dk/dT (which §14 asks for) but NOT d(omega)/d(Y), and Sp stays 0.
    Recorded as debt rather than quietly deferred.
F2  NO EQUILIBRIUM CONSTANT. §20 and §22 say reverse kinetics are INDEPENDENTLY SUPPLIED, not
    derived from a K_eq -- "Do not implement equilibrium thermodynamics unless P18.5's frozen gate
    explicitly requires it", and G6 does not. So a reversible reaction carries TWO independent
    Arrhenius parameter sets, and detailed balance is NOT enforced. That is a real modelling
    limitation and is stated as one.
F3  ONE-WAY TEMPERATURE DEPENDENCE ONLY (§29). P18.5 READS T and must not feed reaction heat back
    into it. The loop reaction -> heat -> T -> reaction is P18.6's.
F4  `A`'s UNITS DEPEND ON REACTION ORDER. §2 warns against claiming one dimensional form for every
    reaction, and it is right: for total reactant order m, [k] = (m^3/mol)^(m-1) / s. So the units
    document must state the order dependence rather than a single formula.
```
