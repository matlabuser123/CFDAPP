# P18.5 — determinism (§36)

```text
ACCUMULATION ORDER IS FIXED BY CONSTRUCTION: ascending reaction index, then ascending cell, over
dense vectors. There is no reduction, no atomic, no parallel region and no map iteration anywhere
in src/species/ArrheniusKinetics.cpp, so repeated evaluation is bitwise identical rather than
identical to a tolerance.

THIS IS A PROPERTY OF THE CODE, NOT A MEASUREMENT RESULT -- but it is measured anyway, because
"there is no reduction" is a claim about code someone will later change.
```

---

## 1. Measured

| route | scope | bitwise differences |
|---|---|---:|
| probe (`logs/04_probe.log`) | 200 repeats × 256 cells × (1 rate + 3 sources) = **204 800 comparisons** | **0** |
| test (`KineticsAreBitwiseDeterministic`) | 50 repeats × 32 cells × (2 rates + 4 sources) | **0** |

```text
BOTH ROUTES RECONSTRUCT THE MECHANISM AND THE KINETICS ON EVERY REPEAT, rather than reusing one
object. That is the point: if ReactionKinetics' name-to-index reordering were order-dependent --
for example if it used an unordered container's iteration order -- reusing one object would hide
it completely, and every repeat would agree while two separately built objects disagreed.

The test additionally uses a DIFFERENT exponent per reaction (n = +0.5 and n = -0.5) so the two
reactions cannot accidentally produce identical values and mask a mix-up.
```

Comparison is `EXPECT_EQ` on doubles — exact equality, not `EXPECT_NEAR`. A tolerance here would
defeat the purpose.

## 2. What determinism does *not* cover

```text
SINGLE-CONFIGURATION, SINGLE-THREAD. These runs are Release+CUDA-OFF host code on one thread.

P18.5 HAS NO GPU PATH AND NO OpenMP REGION, so there is nothing to establish CPU/GPU equivalence
for -- but that is a statement about today's code, and it is the reason the CPU-only
configuration is in the regression (CLAUDE.md: a file reachable only under one build option is
compiled by no routine gate). The three-configuration regression covers Release+CUDA, CPU-only and
Debug+GUI; see regression.md.

CROSS-CONFIGURATION BITWISE EQUALITY IS NOT CLAIMED. Different optimisation levels may contract
a*b+c into an FMA differently, and claiming bitwise equality across build types would be claiming
something neither measured nor required.
```

## 3. Reaction ordering (§37)

Determinism under reordering is a *separate* property from repeat determinism, and it is covered
structurally rather than statistically:

```text
PARAMETERS BIND BY NAME and are reordered into mechanism order at construction, so the SUPPLIED
order cannot affect any result. N8_KineticsBindToReactionsByNameNotByPosition supplies two
parameter sets in REVERSE order with deliberately different activation energies (5e4 vs 1.5e5) and
asserts each reaction keeps its own -- forward(0).activationEnergy() == 5e4.

A positional binding would return 1.5e5 there, and every subsequent rate would be plausible and
attached to the wrong reaction.
```
