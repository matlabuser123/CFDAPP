# P18.7 — mass-fraction closure (§7, §8, §33)

```text
NO NORMALIZATION. NO CLIPPING. Both are grep-verified, not asserted in prose.
```

---

## 1. `ΣY_i = 1`

Measured at the stages §7 names — initial, after the first reacting update, intermediate, final —
for the batch reactor, the reacting flow, and every grid and time-step variant.

| case | `max\|ΣY−1\|` | bound |
|---|---|---|
| batch reactor, 16 cells × 1000 steps | within `massFractionSumTolerance(2)` | `4·eps·2` |
| plug flow, 100/200/400 cells | within the same | `4·eps·2` |
| thermoneutral flow | within the same | `4·eps·2` |

```text
THE BOUND IS P18.1's OWN, inherited verbatim rather than restated. P18.1 ENFORCES it: a state that
violates it is REFUSED at construction, so these measurements are of a quantity that cannot
silently drift -- the march simply stops. That is what bounds the step budget
(known_limitations.md section 4).
```

## 2. Bounds

```text
0 <= Y_i <= 1, BOOLEAN, no tolerance -- P18.1's rule, inherited.

min and max over every species, cell and case are recorded by the drivers and asserted. A
violating state is REFUSED, never clipped; control N1 exercises exactly that path, where a
reversed source sign drives Y out of range and P18.1 rejects it.
```

## 3. Normalization: NONE (§7's disclosure requirement)

```text
Section 7: "No post-step renormalization may be used to manufacture the result. If normalization
exists anywhere in the production path, disclose it explicitly."

MEASURED (logs/05_final_integrity.log, and directly):

    grep -rn normalizeMassFractions src/ include/
      src/species/MassFractionState.cpp:196      the DEFINITION
      include/cfd/species/MassFractionState.hpp:156   the DECLARATION

TWO OCCURRENCES, AND NEITHER IS A CALL. Nothing in the production path invokes it. P18.1 designed
it that way deliberately -- a separate named free function that nothing calls implicitly -- and
P18.7 confirms the property still holds rather than trusting the design note.

The P18.7 drivers do not call it either, and the integrity check asserts that with a positive
control that the grep reads the file.
```

## 4. Invalid inlet composition (§33)

```text
A composition that does not sum to one is REFUSED by MassFractionState at construction, so an
invalid inlet cannot enter a conservation test at all:

    MassFractionState(registry, 4, {0.6, 0.6})  -> InvalidArgumentError
    MassFractionState(registry, 4, {0.3, 0.3})  -> InvalidArgumentError

both asserted in N4_N5_N9_BreakingTheSumOrTheStoichiometryIsDetected. The guard is the type's, not
the test's, which is why no inlet-validation code exists in the drivers.
```
