# P18.7 — determinism (sections 41, 42, 43)

```text
BITWISE, and the comparison is EXACT EQUALITY on doubles, never EXPECT_NEAR.
```

---

## 1. Measured

| case | repeats | compared | result |
|---|---:|---|---|
| **batch reactor** | 20 | full histories: `Y_A`, `T`, `q`, total energy at every one of 200 steps | **0 bitwise differences** |
| **reacting flow** | 5 | every cell's `Y_A` and `T`, plus the species and energy imbalances, plus **the outer-iteration count** | **0 bitwise differences** |

## 2. Everything is rebuilt on every repeat

```text
Each repeat reconstructs the registry, the mixture, the formation enthalpies, the mechanism and
the kinetics. Reusing one set of objects would measure only that the arithmetic is deterministic,
which was never in doubt; rebuilding measures that the SETUP is too -- a name-to-index binding
that depended on container iteration order would be invisible otherwise.

This is the pattern P18.5 and P18.6 established, applied to the integrated case.
```

## 3. The iteration count is part of the claim (section 42)

```text
Section 42: "Do not merely compare final scalar summaries."

The reacting-flow comparison includes outerIterations. A Picard loop that converged in a different
number of iterations would have taken a different path to the same answer -- which is a
determinism failure even when the final field matches, and is invisible to a field-only
comparison.
```

## 4. Separate-process determinism: NOT MEASURED (section 43)

```text
Section 43 asks for it "where practical and part of CFDApp's determinism expectations", and adds:
"If not part of the frozen gate, report but do not invent a new requirement after measurement."

G10 does not require it. It was NOT measured, and that is stated here rather than left implied.
