# P18.4 — invalid chemistry guards (§3, §4, §9, §23, §24, §25, §26)

```text
EVERY GUARD REFUSES. Nothing is repaired, renormalised, clamped or silently accepted.
```

---

## 1. Rejected at reaction construction

| Defect | § | Behaviour |
|---|---|---|
| unknown species name | 3 | refused, naming the reaction and the species |
| empty-string species name | 3 | refused — a naive name lookup might accept it |
| negative stoichiometric coefficient | 4 | refused, **never silently `abs()`-ed** |
| NaN / Inf coefficient | 4 | refused |
| no reactants, or no products | 26 | refused |
| net stoichiometry entirely zero after cancellation | 26 | refused |
| empty reaction name | 25 | refused — reaction identity is user-visible |

```text
THE NEGATIVE-COEFFICIENT MESSAGE SAYS WHAT TO DO INSTEAD:
  "...must be >= 0. A negative coefficient is not silently treated as its magnitude -- move the
   species to the other side of the reaction instead."
because the correct repair is a modelling decision, not an arithmetic one.
```

## 2. Rejected at mechanism construction

| Defect | § | Behaviour |
|---|---|---|
| mass-unbalanced reaction | 9 | refused, **not renormalised**, with the defect and stoichiometry reported |
| duplicate reaction name | 25 | refused, **not silently overwritten** |
| empty mechanism | — | refused |
| a registry whose ordering differs from the mixture's | 35 | refused |

```text
THE LAST ONE MATTERS MORE THAN IT LOOKS. Ordering is part of a SpeciesRegistry's value (P18.1), so
a mismatch would apply the wrong molecular weight to every species -- silently, producing entirely
plausible numbers. Refusing it is how section 35's "a mismatch must be impossible by design" is
achieved for the one case design alone cannot prevent.
```

## 3. Rejected at source evaluation

| Defect | § | Behaviour |
|---|---|---|
| NaN / ±Inf progress rate | 24 | refused, naming the reaction and the cell |
| negative rate on an **irreversible** reaction | 24 | refused, with the fix named |
| mismatched rate-field or cell counts | — | refused |

The irreversible message names the fix: *"Declare it Reversible if a net negative rate is
intended."*

## 4. Bounded chemistry step — DEFERRED, and labelled (§23)

```text
Section 23 asks P18.4 to DETECT whether an explicit update would drive a species negative, and
explicitly says "Do not build a chemistry integrator yet."

PROVIDED:  projectNegativeSpecies(sources, state, density, dt) reports whether
           rho Y_i + dt omega_i < 0 anywhere, and which species and cell is worst.
NOT DONE:  no clamping, no step, no integrator.

In the words section 23 asks for:
    RATE VALID
    TIME INTEGRATION / BOUNDED CHEMISTRY STEP DEFERRED

MEASURED: a large rate with dt = 1 drives species A negative and is DETECTED; the same sources with
dt = 1e-6 are not. And the test asserts THE SOURCE IS UNCHANGED BY EITHER CALL -- the diagnostic
does not clamp, which is the property that makes it a diagnostic rather than a repair.
```

## 5. What is deliberately NOT guarded

```text
ZERO INPUT COEFFICIENTS ARE ACCEPTED. "0 A" is meaningless but harmless, and rejecting it would
make a programmatically generated mechanism harder to build than it needs to be. A reaction that is
EMPTY AFTER canonicalisation is refused, which is the case that actually matters. The policy is
documented in source_formulation.md section 3 because the opposite choice is equally defensible.

A SPECIES REPEATED ON ONE SIDE IS SUMMED, not rejected: 2A + A is 3A. Also a documented choice.

A CATALYST -- a species on both sides cancelling to nu_i = 0 -- IS ALLOWED, and is
indistinguishable from an uninvolved species in the net vector. That is correct: a catalyst
contributes nothing to any species source. The one-sided coefficients are retained so a caller can
still tell the two apart for reporting.
```
