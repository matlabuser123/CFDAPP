# P18.1 — validation

```text
LABEL     Per CLAUDE.md section 4, every check below is labelled. P18.1 is a STATE phase, so most
          of what follows is VERIFICATION (the implementation does what the frozen gate specifies)
          and CONSISTENCY, plus one COMPATIBILITY check against real shipped output.

          THERE IS NO PHYSICAL VALIDATION IN P18.1, AND NONE IS CLAIMED. A bounded, sum-to-one
          composition is an algebraic invariant, not a physical result. Validation against
          analytical or literature results belongs to the phases that transport and react the
          state -- P18.2's canonical/manufactured tests and P18.7's conservation suite.
```

---

## 1. Focused tests

```text
BINARY            build/final/tests/unit/species/CFDSpeciesTests   (Release, CUDA OFF, OpenMP OFF)
                  rebuilt from a DELETED binary, so a compile failure could not leave a stale one
RUN               logs/02_focused_tests.log

  SpeciesRegistryTest      12 / 12 passed     VERIFICATION
  MassFractionStateTest    48 / 48 passed     VERIFICATION + CONSISTENCY + COMPATIBILITY
  pre-existing species     37 / 37 passed     REGRESSION
  ---------------------------------------------------------
  total                    97 / 97 passed, 0 failed
```

## 2. Quantitative report (§37)

All values measured by `tools/p18_1_measure.cpp` against the built library, not transcribed by
hand. Raw output: `logs/03_measurements.log`.

```text
eps                                2.2204460492503131e-16
chosen sum tolerance               4 * eps * N_species   -- INHERITED from frozen G2(a), not
                                   chosen here. N=1: 8.8817841970012523e-16
                                                N=2: 1.7763568394002505e-15
                                                N=3: 2.6645352591003757e-15
                                                N=64: 5.6843418860808015e-14

max valid sum error                1.7763568394002505e-15   =  8.000 eps   ACCEPTED
                                   (exactly the N=2 bound -- the criterion is tight, not slack)
smallest rejected sum error        1.9984014443252818e-15   =  9.000 eps   REJECTED
discrimination width               1 eps

largest accepted Y_i               1        ({1.0, 0.0} -- a pure species)
smallest accepted Y_i              0        (an absent species)

invalid sums detected              17
invalid bounds detected             4
NaN / Inf detections                4
```

```text
REAL SHIPPED DATA (cases/species_diffusion/results/fields.csv, concentration_CO2, 80 cells)
  minimum value    2.49999680492757233e-02
  maximum value    9.74999968833643260e-01
  all 80 within [0, 1]                                    YES
  valid as a ONE-species mixture                          NO  -- correctly rejected (see below)
  valid with an explicit carrier, values preserved        YES -- BITWISE, all 80 cells
```

## 3. The ULP ladder — §30 non-vacuity

```text
  0 eps   deviation 0                        ACCEPTED
  1 eps   deviation 2.2204460492503131e-16   ACCEPTED
  2 eps   deviation 4.4408920985006262e-16   ACCEPTED
  3 eps   deviation 6.6613381477509392e-16   ACCEPTED
  4 eps   deviation 8.8817841970012523e-16   ACCEPTED
  5 eps   deviation 1.1102230246251565e-15   ACCEPTED
  6 eps   deviation 1.3322676295501878e-15   ACCEPTED
 16 eps   deviation 3.5527136788005009e-15   REJECTED
 17 eps   deviation 3.7747582837255322e-15   REJECTED
 18 eps   deviation 3.9968028886505635e-15   REJECTED
 19 eps   deviation 4.2188474935755949e-15   REJECTED
 20 eps   deviation 4.4408920985006262e-15   REJECTED
```

The composition is stepped in **ULPs**, so nothing depends on a hand-picked magnitude. The
criterion accepts up to exactly its bound and rejects immediately beyond it. `9 eps ≈ 2e-15` is
already rejected — roughly twelve orders of magnitude tighter than the `1e-3` §30 warns against,
and C5 confirms that widening it to `1e-3` is caught by two tests.

## 4. Per-cell detection — §16

```text
PLANTED   one bad cell in 100000 (b[99999] = 0.5 + 1e-9). Neither field's global min or max moves.
DETECTED  "mass fractions at cell 99999 sum to 1.0000000010000001; must be within
           1.7763568394002505e-15 of 1 (deviation 1.000000082740371e-09)"
CONTROL   AValidThousandCellStatePasses -- same construction, no planted defect, passes.
```

A global `{min, max}` diagnostic — which is what the pre-P18 `concentrationBounds` offers — cannot
see this at all.

## 5. Determinism — state layer only

```text
MEASURED   compositionSum() of the same cell over 100 repeats: BITWISE identical
           the same sum across a copy of the state: BITWISE identical
MECHANISM  fixed ascending-species-index accumulation, left to right, in one function that
           validate() also calls. No unordered iteration, no parallel reduction.
SCOPE      THIS IS NOT G10. G10 is solver-level determinism across processes and launch
           configurations, and belongs to P18.8/P18.9. P18.1 claims only that its own validation
           is reproducible.
```

## 6. Negative controls

Summarised here, detailed in `negative_controls.md`:

```text
C1 bounds removed         DETECTED  (4 tests)
C2 sum removed            DETECTED  (5 named, 8 total)
C3 finiteness removed     DETECTED  (RejectsNaN; RejectsInf correctly still passes)
C4 identity weakened      DETECTED  (2 tests)  -- first run reported VACUOUS; harness bug, preserved
C5 threshold widened      DETECTED  (2 tests)
production source after   byte-identical to pre-control, sha256 verified
```

## 7. Formatting

```text
the five P18.1 files          clang-format-18 --dry-run --Werror   exit 0
the tree as CI runs it        28 pre-existing offenders, NONE of them a P18.1 file
                              inherited from the uncommitted P14-P17 tree; NOT fixed here
```

## 8. What is NOT validated

```text
NO PHYSICAL VALIDATION        stated at the top. There is no transport, no analytical solution to
                              compare against, and no manufactured solution, because P18.1
                              implements no PDE.
NO CONSERVATION CLAIM         G3 is not claimable from a state type -- there is nothing to
                              conserve until something transports it.
NO GRID/TIME-STEP SENSITIVITY no discretization exists in this phase.
NO CPU/GPU EQUIVALENCE        no GPU code was written; G9 is P18.8's.
NO SOLVER DETERMINISM         see section 5.
NO PERFORMANCE CLAIM          section 32 -- performance is explicitly not a gate here, and no
                              layout decision was made for it.
```
