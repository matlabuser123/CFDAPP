# P18.4 — negative controls (§42)

```text
All nine are PERMANENT TESTS, not source mutations. Every defect §42 names is expressible through
the production API or as an alternative arithmetic expression evaluated alongside the correct one,
so each control runs in every regression and cannot rot.

Each reports its MARGIN, so "detected" is quantified rather than asserted.
logs/02_reaction_probe.log.
```

---

## 1. The nine

| # | Defect | Margin measured | Result |
|---|---|---|---|
| **N1** | flip / perturb a stoichiometric coefficient | residual **281 475×** over tolerance | **DETECTED** |
| **N2** | omit the molecular-weight multiplication | **16×** for species A | **DETECTED** |
| **N3** | wrong molecular weight for one species | 5% error → mechanism **refuses** | **DETECTED** |
| **N4** | accept a mass-unbalanced reaction | defect 0.03125, a third of the product mass | **DETECTED** |
| **N5** | omit one reaction during accumulation | species C source goes to exactly 0 | **DETECTED** |
| **N6** | reverse the forward/reverse subtraction | irreversible refuses q<0; reversible accepts | **DETECTED** |
| **N7** | allow an unknown species | refused, three ways | **DETECTED** |
| **N8** | accept a NaN/Inf progress rate | refused, three values | **DETECTED** |
| **N9** | wrong mol/kmol unit factor | exactly **1000×** | **DETECTED** |

## 2. N1 — G6's own control, and it is brutal

G6 states it directly: *"a stoichiometric coefficient perturbed must FAIL the closure criterion."*

```text
A + 2B -> C, with the B coefficient perturbed to 2 + 1e-9:

    mass residual   3.125000e-11
    tolerance       1.110223e-16       (4 eps * max|M nu|)
    over by         281 475 x
```

A perturbation of one part in `1e9` — far below anything a human reading a mechanism file would
notice — is caught by five orders of magnitude. The criterion is not close to vacuous.

## 3. N2 and N9 — the two unit errors, and why both are needed

```text
N2  omega = M nu q, not nu q. For species A, M = 0.0625, so omitting it INFLATES the source 16x.
N9  M is [kg/mol]. A kmol value is 1000x larger, so omega is wrong by exactly 1000x.
```

```text
THEY ARE DIFFERENT ERRORS AND BOTH ARE PLAUSIBLE. N2 is forgetting the conversion entirely; N9 is
doing it in the wrong unit system. N9 also announces itself a second way: a molecular weight of
62.5 kg/mol is not a plausible species, which the test asserts alongside the ratio.
```

## 4. N3 and N4 — the mass-balance pair

```text
N3  perturb ONE species' molecular weight by 5%. The reaction's stoichiometry is unchanged and
    still looks correct, but sum_i M_i nu_i is no longer zero, so the MECHANISM refuses it. This is
    the control for section 35's "a mismatch must be impossible by design": P18.4 stores no
    molecular weights of its own, so the only way to get a wrong one is to give the MIXTURE a wrong
    one -- and that is caught here.
N4  a genuinely unbalanced reaction, A + B -> C, where M_A + M_B = 0.09375 against M_C = 0.125.
    The defect is 0.03125 -- a THIRD of the product mass, not a subtle rounding.
    The Reaction OBJECT still constructs (stoichiometry alone is not wrong); the MECHANISM refuses
    it, because that is where molecular weights enter. The separation is deliberate.
```

## 5. N5 and the cancellation trap

```text
N5  dropping one reaction from a two-reaction mechanism must change omega. Asserted on the chain
    A -> B -> C: with the second reaction omitted, species C's source becomes EXACTLY 0, because
    C is only produced by the dropped reaction.

AND THE COMPANION TEST that keeps N5 honest (section 41): a mechanism where two contributions
CANCEL for one species. B is produced by A->B at rate q and consumed by B->C at the same q, so its
net source is exactly 0 -- and the test then asserts THE INDIVIDUAL CONTRIBUTIONS WERE LARGE
(> 0.1), because "the result is zero" proves nothing if both terms were zero to begin with.
```

## 6. N6 — the two directions carry different semantics

```text
IRREVERSIBLE  q = q_f, and q_f >= 0 is ENFORCED. A negative forward rate is not a slow reaction, it
              is a reaction running backwards, which an irreversible one cannot do. REFUSED.
REVERSIBLE    q = q_net = q_f - q_r, ANY SIGN. Negative means net reverse. ACCEPTED.

The control asserts BOTH halves on the SAME reaction, differing only in its declared direction --
so it proves the check is direction-dependent rather than absent or universal.

Direction symmetry is asserted separately and BITWISE: omega_i(-q) = -omega_i(q) exactly, because
negating q negates each product exactly.
```

## 7. N7 and N8 — the input guards

```text
N7  a reaction cannot introduce a species the registry does not hold. Three refusals: an unknown
    name among the reactants, among the products, and the EMPTY-STRING name that a naive lookup
    might accept. Consequence: a mechanism can never address a species the state and the mixture do
    not share.
N8  NaN, +Inf and -Inf progress rates all refused, with the reaction and the cell named.
```

## 8. What is NOT controlled, and why

```text
Arrhenius exponent / activation-energy controls   G6 lists these, and they are P18.5's. P18.4
                                                  implements no kinetics, so there is nothing to
                                                  perturb -- a control here would have no subject.
heat-release controls                             P18.6's. No energy source exists.
elemental-balance controls                        no elemental composition exists (section 10).
source-mutation harness                           NOT USED. Every defect above is expressible
                                                  through the API or as an alternative expression,
                                                  so the controls are permanent tests that run in
                                                  every regression rather than a script that
                                                  mutates source and restores it.
```
