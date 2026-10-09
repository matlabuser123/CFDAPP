# P18.4 — Reaction Source Framework — SUMMARY

```text
P18.4 STATUS: COMPLETE

GATE        results/p18/gates/P18_GATES.md
            sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
            re-verified UNCHANGED at open and at close. No threshold in it was edited.
            No gate amendment was needed.

G6 criterion (b)  CLOSED -- the stoichiometric closure, sum_i W_i nu_i = 0 to 4 eps * max|W_i nu_i|,
                  with G6's OWN negative control measured at 281 475x over tolerance.
G6 OVERALL        NOT CLOSED, deliberately. Its criterion (a) is a RATE comparison against an
                  independent Arrhenius implementation and its own `phase` field says P18.5.
                  P18.4 implements no kinetics, so claiming "G6 PASS" would be the over-claim the
                  gate's phase field guards against.
G3                EVIDENCE TOWARD -- sum_i omega_i = 0 per cell, and closed-reactor mass
                  conservation. G3's phase is P18.7.
G1 · G2 · G10     PASS for what this phase touches.
NOT CLAIMED       G7 G8 G9 G11 (section 44).

TESTS       203 / 203 in CFDSpeciesTests. 38 new.
```

---

## 1. Reaction architecture

```text
reaction type                 cfd::species::Reaction
species reference             the authoritative P18.1 SpeciesRegistry, resolved by name ONCE at
                              construction. A reaction keeps NO species list of its own.
stoichiometric representation a DENSE net vector nu_i over the registry, plus the two one-sided
                              vectors for reporting. No map is iterated for arithmetic.
progress-rate interface       ReactionProgressRates -- one ScalarField per reaction, q per cell.
                              P18.4 does NOT compute q.
mechanism type                cfd::species::ReactionMechanism -- validates every reaction
                              individually at construction.
```

## 2. Source formulation

```text
nu_i convention          nu_i = nu''_i - nu'_i ;  < 0 consumed, > 0 produced, = 0 uninvolved or a
                         cancelled catalyst
molar source             omega_dot_i,molar = nu_i q                [mol/(m^3 s)]
mass source omega_i      omega_i = M_i nu_i q                      [kg/(m^3 s)]
multi-reaction           omega_i = sum_r M_i nu_{i,r} q_r, accumulated in ASCENDING reaction index
                         then ASCENDING species index, over dense vectors
units                    q [mol/(m^3 s)] · M_i [kg/mol] · omega_i [kg/(m^3 s)]
                         ONE system, mol never kmol. M_i comes from P18.3; P18.4 stores none.
Patankar split           Su = omega_i, Sp = 0. Linearising omega in Y needs a kinetics derivative
                         P18.4 does not have; the implicit part is P18.5's.
```

## 3. Conservation

```text
max per-reaction |sum_i M_i nu_i|       0                        (dyadic weights, exact)
                                        0                        (round-off weights)
                                        tolerance 1.110223e-16 / 9.989799e-17
max per-cell |sum_i omega_i|            0                        (dyadic weights)
                                        1.1102230246251565e-16   (round-off weights)
                                        relative 1.2338538725026906e-16, allowance 8 eps
closed-reactor total-mass drift         within 8 eps N_cells
```

```text
TWO MECHANISMS WERE NEEDED, AND THE SECOND IS THE POINT. Dyadic molecular weights (2^-4, 2^-5,
2^-3) make M_A + 2 M_B = M_C EXACT in binary, so every residual is exactly 0 -- the sharpest
statement, but one that NEVER EXERCISES THE TOLERANCE. Weights that balance only to round-off were
therefore found by search outside CFDApp, and on those the per-cell sum is 1.11e-16: non-zero,
within allowance, and measured rather than bypassed. Reporting only the dyadic zeros would have
been a vacuous pass.
```

## 4. Reversible support

```text
forward-only result        q_net > 0 : A consumed, C produced
reverse-dominant result    q_net < 0 : A PRODUCED, C CONSUMED
equal forward/reverse      q_f - q_r = 0 : every omega_i EXACTLY 0
sign symmetry error        0 -- omega_i(-q) = -omega_i(q) asserted BITWISE
```

The equilibrium case is built as `q_f - q_r` with the two equal, so the zero arises from the
subtraction rather than from passing a literal 0. **It claims nothing about thermodynamic
equilibrium** — P18.4 has no equilibrium constant.

## 5. Invalid chemistry — every guard refuses

```text
unknown species          refused, 3 ways (reactants, products, empty-string name)
negative coefficient     refused, NEVER silently abs()'d; the message names the correct fix
mass-unbalanced reaction refused, NOT renormalised; diagnostic names the reaction, the defect and
                         the offending stoichiometry species by species
NaN / Inf                refused, in coefficients and in progress rates
invalid rate             a NEGATIVE rate on an IRREVERSIBLE reaction is refused; the same value on
                         a Reversible one is accepted -- so the check is provably
                         direction-dependent rather than absent or universal
duplicate reaction       refused, NOT silently overwritten
empty / net-zero reaction refused
mismatched registry      refused -- ordering is part of a registry's value, and a mismatch would
                         apply the wrong molecular weight to every species, silently
```

## 6. Negative controls — 9/9, with margins

```text
N1 stoichiometric coefficient perturbed by 1e-9   residual 281 475x over tolerance   DETECTED
N2 molecular-weight multiplication omitted        16x for species A                  DETECTED
N3 wrong molecular weight for one species         5% -> mechanism refuses            DETECTED
N4 mass-unbalanced reaction accepted              defect = 1/3 of the product mass   DETECTED
N5 one reaction omitted from accumulation         species C source -> exactly 0      DETECTED
N6 forward/reverse subtraction reversed           direction-dependent refusal        DETECTED
N7 unknown species allowed                        3 refusals                          DETECTED
N8 NaN progress rate accepted                     NaN, +Inf, -Inf all refused         DETECTED
N9 wrong mol/kmol factor                          exactly 1000x                       DETECTED
```

```text
N1 IS G6's OWN CONTROL and it is brutal: a perturbation of one part in 1e9 -- far below anything a
human reading a mechanism file would notice -- is caught by five orders of magnitude.

THE CANCELLATION TEST KEEPS ITSELF HONEST (section 41): where two reactions cancel for one species,
the test also asserts the INDIVIDUAL CONTRIBUTIONS WERE LARGE (> 0.1), because "the result is zero"
proves nothing if both terms were zero to begin with.
```

## 7. Determinism

```text
repeat count                200 (probe) · 50 (test, RECONSTRUCTING the mechanism each time)
source difference           0
accumulation difference     0
bitwise                     YES, for omega_i, Sp, and the net stoichiometry after reconstruction
mechanisms                  dense storage (no map iterated for arithmetic) · fixed accumulation
                            order · consistent grouping (M*nu)*q · exact skips for zero rates and
                            zero coefficients · no cached state
```

## 8. Regression

```text
focused P18.4   203 / 203 passed (38 new · 34 P18.3 · 34 P18.2 · 60 P18.1 · 37 pre-P18)
P18.1           60 / 60     P18.2   34 / 34     P18.3   34 / 34
P15             163 / 163   P17     215 / 215 (TotalEnergy 34/34)
P14 / P16       22 / 22  ·  246 / 246

Release+CUDA    2929 selected · 2878 Passed · 0 FAILED · 2 Skipped · 49 DISABLED · 2057 s · exit 0
CPU-only        2706 selected · 2659 Passed · 0 FAILED · 0 Skipped · 47 DISABLED · 1500 s · exit 0
Debug+GUI       2991 selected · 2937 Passed · 3 FAILED · 2 Skipped · 49 DISABLED · 8131 s · exit 8
```

```text
ALL THREE MATCH THE PREDICTION EXACTLY, including the Debug+GUI branch. +38 selected and +38 Passed
in every configuration; Skipped and DISABLED did not move anywhere.

The three Debug+GUI failures are the RELIABLE inherited set. The marginal
DriverSeegmiller.MeshSequence passed at 7034.57 s -- 97.7% of the unchanged 7200 s cap -- making a
FOURTH measurement of that test: 7123 s pass, 7202 s timeout, 6765 s pass, 7035 s pass.
```

## 9. Repository changes

```text
production added      include/cfd/species/ReactionMechanism.hpp
                      src/species/ReactionMechanism.cpp
production modified   NONE. src/CMakeLists.txt gained one target_sources block.
tests added           tests/unit/species/test_reaction_mechanism.cpp (38 tests)
test files modified   tests/unit/species/CMakeLists.txt (+1 line)
evidence              results/p18/reaction_source/ -- 11 docs, 6 logs, 6 tools, 1 data file
gate amendments       NONE.
threshold changes     NONE. The closure bound is G6's, inherited verbatim.
```

## 10. Integrity

```text
git diff --check        260 warnings, 0 in a P18.4 file (259 in three pre-existing CMakeLists,
                        1 in a TODO.md line P18.0 wrote). PUSH BLOCKER, already in TODO.md.
frozen gate             re-hashed at close, unchanged
validation noise        50 files rewritten by the Debug run, RESTORED; wall_shear changes 0.
Arrhenius added early   NO -- 0 for activationEnergy, preExponential, std::exp,
                        temperatureExponent. The `Arrhenius` and `exp(` hits are TWO COMMENTS
                        saying P18.5 owns them, resolved by inspection in logs/05_.
heat release added early NO -- 0 for heatRelease, reactionEnthalpy, heatOfFormation,
                        formationEnthalpy, energySource.
duplicated M data       NO -- 0 molecular weights stored in the reaction layer, 4 read from P18.3.
second assembly path    NO -- 0 new assembler functions; SpeciesSources and zeroSpeciesSources
                        reused from P18.2.
silent correction       NO -- the one `normali` hit is the REJECTION message saying stoichiometry
                        is NOT renormalised; the one `clamp` hit is the comment saying nothing is
                        clamped.
P18.2 transport         UNTOUCHED -- 0 changes, sha256 recorded.
P17 shock               UNTOUCHED -- HllcFlux, TotalEnergy, IdealGasEOS, ThermodynamicProperties
                        hashes recorded. P17 G6/G10 NOT reopened.
deleted tests           NONE. DISABLED_ added: 0.
commit / push           NONE. Not authorized.
```

## 11. Next

```text
P18.5 — Arrhenius Chemistry
NOT AUTHORIZED YET
```

Carried forward:

```text
1. G6 IS ONLY HALF CLOSED. Its rate criterion -- omega_i against an independent arbitrary-precision
   Arrhenius implementation over a matrix of T, p and composition, to 32 eps -- is P18.5's, and so
   are two of its three negative controls. P18.4 closed the stoichiometric half and says so.
2. ELEMENTAL VALIDATION DEFERRED, MASS CONSERVATION ENFORCED. No elemental composition exists in
   the repository; section 10 says not to invent one. P18.7 can own it.
3. THE BOUNDED CHEMISTRY STEP IS DEFERRED. projectNegativeSpecies DIAGNOSES where
   rho Y_i + dt omega_i would go negative; nothing is clamped and no integrator exists. P18.5/P18.6.
4. THE IMPLICIT PATANKAR Sp IS STILL ZERO. A stiff rate wants one, and it needs d(omega)/d(Y) --
   a kinetics derivative P18.5 will have.
5. CASE-FILE REACHABILITY. No case key defines a mechanism and nothing in production produces a
   progress rate. The same limit P18.2 and P18.3 carry.
6. THE MARGINAL DEBUG+GUI TIMEOUT now has FOUR measurements spanning both sides of the cap.
```
