# P18.1 — invariants

Every threshold here was **inherited verbatim** from `results/p18/gates/P18_GATES.md` G2, frozen
before any P18.1 code existed. Its sha256 was re-verified at P18.1 start as
`921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3` — unchanged. **P18.1 derived no
threshold of its own and adjusted none.**

---

## 1. The three invariants, as implemented

```text
finiteness   std::isfinite(Y_i) at every (species, cell).            NaN and Inf rejected.
bounds       0 <= Y_i <= 1.                                          boolean, NO tolerance.
sum          |sum_i Y_i - 1| <= 4 * eps * N_species, PER CELL.
eps          std::numeric_limits<double>::epsilon()
             measured: 2.2204460492503131e-16
```

`massFractionSumTolerance(N)` is the single definition, so production, tests and this document all
quote the same number and none can drift independently.

| N_species | tolerance = 4·eps·N | measured |
|---:|---|---|
| 1 | 4 eps | 8.8817841970012523e-16 |
| 2 | 8 eps | 1.7763568394002505e-15 |
| 3 | 12 eps | 2.6645352591003757e-15 |
| 64 | 256 eps | 5.6843418860808015e-14 |

## 2. Why the sum criterion is not zero, demonstrated rather than asserted

G2(a) says the bound is deliberately not zero because "a normalised set cannot be exactly 1 in
binary floating point for general N". P18.1 measured a case where that bites:

```text
{0.03, 0.282, 0.688}   exactly 1 in EXACT arithmetic (30/1000 + 282/1000 + 688/1000)
                       NOT 1 in binary: deviation 1.1102230246251565e-16 = 0.5 eps
                       ACCEPTED, against a 12 eps bound for N=3.
```

The deviation was computed independently in Python, outside CFDApp, before the test was written.

**A counter-example worth recording, because it cost a test failure.** `{1/3, 1/3, 1/3}` sums to
*exactly* 1.0. `3 × fl(1/3)` is `(2^54 − 1)·2^-54 = 1 − 2^-54`, which is the exact tie between
`1 − 2^-53` and `1`, and round-half-to-even picks `1`. Thirds are therefore **not** a round-off
example. P18.1's first test run asserted that they were and failed; the failure is preserved at
`logs/01_first_failure_PRESERVED.log` and the fact is now pinned by its own test
(`AThirdsCompositionHappensToSumExactly`).

## 3. Non-vacuity of the sum criterion — §30

§30 requires proof that the tolerance distinguishes a round-off-valid composition from a
meaningfully invalid one. Measured by stepping one entry of `{0.5, 0.5}` across the bound in
**ULPs**, so nothing depends on a hand-picked magnitude (`logs/03_measurements.log`):

```text
  0 eps  deviation 0                          ACCEPTED
  1 eps  deviation 2.2204460492503131e-16     ACCEPTED
  2 eps  deviation 4.4408920985006262e-16     ACCEPTED
  3 eps  deviation 6.6613381477509392e-16     ACCEPTED
  4 eps  deviation 8.8817841970012523e-16     ACCEPTED
  5 eps  deviation 1.1102230246251565e-15     ACCEPTED
  6 eps  deviation 1.3322676295501878e-15     ACCEPTED
 16 eps  deviation 3.5527136788005009e-15     REJECTED
 17 eps  deviation 3.7747582837255322e-15     REJECTED
 20 eps  deviation 4.4408920985006262e-15     REJECTED

max deviation ACCEPTED   1.7763568394002505e-15   =  8.000 eps   (exactly the N=2 bound)
min deviation REJECTED   1.9984014443252818e-15   =  9.000 eps
```

**The criterion bites exactly at the frozen bound, and the discrimination is one eps wide.** It is
neither vacuous nor loose: `9 eps ≈ 2e-15` is already rejected, some twelve orders of magnitude
tighter than the `1e-3` §30 warns against. C5 below confirms that widening it to `1e-3` is caught.

## 4. Bounds are boolean

```text
{-1.0e-12, 1.0 + 1.0e-12}   sum EXACTLY 1, so only the bounds check can fire   REJECTED
{-0.25, 1.25}               sum EXACTLY 1                                      REJECTED
{1.25, -0.25}               sum EXACTLY 1                                      REJECTED
```

Each of these has an exact sum of 1 by construction, which is what makes them bounds tests rather
than sum tests. A tolerance on the bounds check — of any size — would admit the first one.

```text
largest Y_i accepted    1        (a pure species, {1.0, 0.0})
smallest Y_i accepted   0        (an absent species)
```

Both endpoints are admissible: `0 <= Y_i <= 1` is inclusive, and a mixture in which one species is
absent or pure is physically ordinary.

## 5. Finiteness

```text
{NaN, 1.0}    REJECTED   "is not finite"
{+Inf, 1.0}   REJECTED
{-Inf, 1.0}   REJECTED
```

Recorded because C3 exposed it: with the finiteness check removed, **NaN would be silently
accepted** — `value < 0.0 || value > 1.0` is false for NaN, and `|NaN − 1| > tolerance` is also
false, so both later checks wave it through. `±Inf` would still be caught by the bounds check. The
finiteness check therefore guards NaN specifically, and it is not redundant with the others.

## 6. Summation order

Ascending species index, accumulated left to right into one `Real`, in `compositionSum()` — which
`validate()` calls, so the two can never disagree. No unordered iteration, no pairwise or
compensated summation, no parallel reduction. Measured: the sum of the same cell was bitwise
identical over 100 repeats, and identical across a copy of the state
(`ValidationIsDeterministicAcrossRepeats`, `SumIsIdenticalAcrossCopies`).

## 7. Per-cell, not aggregate — §16

```text
one bad cell in 100000, planted at cell 99999 (b[99999] = 0.5 + 1e-9):

  mass fractions at cell 99999 sum to 1.0000000010000001; must be within
  1.7763568394002505e-15 of 1 (deviation 1.000000082740371e-09)
```

Neither field's global min or max moves — species O2 is 0.5 everywhere, species N2 is 0.5 except in
one cell. A `concentrationBounds`-style global {min, max} diagnostic, which is what the pre-P18
species stack offers, **cannot see this at all**. Validation walks every cell and names the first
offender.

## 8. Rejection, not repair — G2(d)

```text
construction from {0.8, 0.8} silently normalised:  NO (rejected)
normalizeMassFractions({0.8, 0.8})              =  {0.5, 0.5}   -- only when asked
```

No constructor and no mutator ever calls `normalizeMassFractions`. A rejected
`setCellComposition` leaves the state **bitwise unchanged**, because the whole composition is
validated before any element is written.

This follows an inherited policy rather than a new one: `SpeciesEquation.hpp` already refuses to
clip ("boundedness violation is a numerical-quality signal a caller reports, not something silently
patched over") and P17.5's `recover()` refuses rather than flooring.
