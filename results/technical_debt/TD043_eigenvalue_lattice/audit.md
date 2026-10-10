# TD-043 — the lattice is DERIVED EXACTLY from the code. And it un-withdraws one of my withdrawals.

```text
opened      2026-10-10, carved out of TD-040's dt ladder
status      MECHANISM CLOSED. Derived from source, confirmed to 0.0000 % against the solver's own
            reported bracket, and verified against 10 independent measurements spanning 63-500
            cells and four dt values. NO solver run was needed to close it.
severity    P1. It resolution-limits every S_L convergence study on record.
evidence    TD040_recentre_conservation/logs/03_dt_ladder_fixed_tree.log
            TD004_premixed_flame/logs/A5_01_c6_c6a.log (fixture_probe, 4 cell counts)
            TD029_error_cancellation/logs/01_dt_dx_separation.log
reproducer  arithmetic only; §1 can be checked with a calculator.
```

---

## 1. The derivation, from three lines of source

```text
PremixedFlame.cpp:1821   massFlux = (high == 0.0) ? massFlux * 2.0 : massFlux * 0.5;
                         PHASE 1 brackets by doubling or halving, so the initial ratio is
                         r0 = high / low = 2 EXACTLY.

PremixedFlame.cpp:1834   massFlux = std::sqrt(low * high);
PremixedFlame.cpp:1861   massFlux = std::sqrt(low * high);
                         PHASE 2 takes the GEOMETRIC midpoint, and reports the bracket's geometric
                         centre. So the ratio transforms r -> sqrt(r), NOT the width w -> w/2.

PremixedFlame.cpp:1832   while (... && (high - low) / low > settings.tolerance)
                         termination is on the RELATIVE width, r - 1 <= tolerance.
```

Geometric halving of the ratio gives `r_n = 2^(1/2^n)`, so with `tolerance = 1e-3`:

```text
 n    r_n = 2^(1/2^n)      r_n - 1
 8    1.002711275050     2.711275e-03
 9    1.001354719892     1.354720e-03
10    1.000677130693     6.771307e-04   <- first value <= 1e-3, so the loop STOPS here
```

```text
DERIVED terminal relative bracket width   6.771306931e-04
the solver REPORTS                        0.000677131
agreement                                 0.0000 %
```

**The reported `bracket = 0.000677131`, constant in every run, is `2^(1/1024) - 1`.** It was never an
absolute mass flux — which is why it looked dimensionally puzzling and why it never varied.

### 1.1 So the lattice is MULTIPLICATIVE, not additive

The achievable eigenvalues form a **geometric** progression of ratio `2^(1/1024)`:

```text
spacing  0.067713 % of S_L, everywhere
```

That is the crucial structural point: the spacing is **relative**, so it is the same fraction at
every mesh and every dt. It is `tolerance`-set, not resolution-set.

## 2. Verified against every S_L measured, with no new runs

Taking `4.955332` as the anchor and `k = ln(S_L / 4.955332) / ln(2^(1/1024))`:

```text
source                      S_L         k          on lattice?
dt ladder L0  (500 cells)   4.955332     0.000     yes
dt ladder L1  (500 cells)   4.958687     1.000     yes
dt ladder L2  (500 cells)   4.958687     1.000     yes
dt ladder L3  (500 cells)   4.962045     2.000     yes
fixture_probe  500 cells    4.95533     -0.001     yes
fixture_probe  250 cells    4.94863     -1.999     yes
fixture_probe  125 cells    4.90527    -15.001     yes
fixture_probe   63 cells    4.51340   -138.001     yes
TD-029 leg B   250 cells    4.988989   10.000     yes
PRE-FIX eigenvalue          4.671937  -87.000     yes
```

**Ten measurements, four cell counts, four dt values, two libraries — every one an integer lattice
point.** This is what closes the mechanism without a single additional solve, and it answers the
other half of the question: **the lattice does NOT change at 125 or 250 cells**, because it is
relative. No measurement at those levels is needed.

### 2.1 The lattice's ANCHOR depends on `initialMassFlux`

Two values are off the default lattice:

```text
C6 test, mdot_0 = 0.1   4.95548    k =  0.044
C6 test, mdot_0 = 0.9   4.95316    k = -0.648
```

Both used a non-default initial mass flux, so PHASE 1's doubling/halving terminated on a *different*
bracket — same ratio 2, different endpoints, hence a different lattice offset. **The ratio is
universal; the phase is not.**

That has a consequence for C6 property 2. Its reported *"initial-guess spread 0.000469"* is
`9.46e-05` relative = **0.14 of a lattice step**, and the two runs sit on differently-anchored
lattices. So that spread is measuring **anchor offset, not eigenvalue sensitivity**. The criterion
still passed for a real reason — a genuine degeneracy would move `S_L` by many steps — but the
number `0.000469` is not a physical quantity and should not be quoted as one.

## 3. I OVER-WITHDREW TD-029's CLAIM. Reinstating it.

In `TD040/verification_dt_ladder.md` §4.1 I wrote that TD-029's *"opposite signs, hence
cancellation"* headline was **unsupported**, on the grounds that a one-quantum change *"cannot
establish a direction"*. **That reasoning is wrong.**

The reported value is the terminal bracket's geometric centre, so it lies within **±0.5 step** of the
true root. An observed difference of `N` steps therefore bounds the true difference to
`(N-1, N+1)` steps:

```text
observed  0 steps -> true in (-1, +1) steps = (-0.068, +0.068) %   sign UNDETERMINED
observed  1 step  -> true in ( 0, +2) steps = (+0.000, +0.135) %   SIGN DETERMINED
observed 10 steps -> true in (+9, +11) steps = (+0.609, +0.745) %  SIGN DETERMINED
```

**A single full step is strictly positive.** So:

```text
TD-029 spatial   500 -> 250 cells   +10 steps   refining dx LOWERS S_L   sign DETERMINED
TD-029 temporal  dt   -> dt/2        +1 step    refining dt RAISES S_L   sign DETERMINED

=> OPPOSITE SIGNS HOLD. TD-029's qualitative claim is REINSTATED.
```

What genuinely does not survive is the **value**: the temporal error is not `+0.068 %` but somewhere
in `(0, +0.135) %`, and the ratio `|temporal|/|spatial|` is not `0.10` but anywhere in
`(0, 0.22)`. And the *dt ladder's* `L1 -> L2` leg gave **0 steps**, so that particular halving's
sign is undetermined — which is a different statement from TD-029's single halving, and I conflated
the two.

**This is the third correction in this chain** — first the candidate explanation, then the
withdrawal, now the withdrawal of part of the withdrawal. The arithmetic in §3 is what should have
been done before the first one.

## 4. C6a's order is UNDETERMINED — neither a pass nor a regression

C6a's differences, in lattice steps:

```text
coarse  125 -> 250   13 steps        true in (12, 14)
fine    250 -> 500    2 steps        true in ( 1,  3)

p = log2(13/2) = 2.700        (fixture_probe reported 2.693)
TRUE p in (2.00, 3.81)
frozen band [1.5, 2.5]        overlaps the interval in (2.00, 2.50]
```

**The band's upper edge lies INSIDE the uncertainty interval.** So this instrument cannot determine
whether `p` is inside or outside `[1.5, 2.5]`. C6a's recorded FAIL stands as a measurement — §5.3
is explicit that explaining a failure does not waive it — but the correct characterisation is
**"undetermined at this tolerance"**, not "the order regressed".

That is a materially different finding from either of my two previous readings of C6a, and it points
at a different remedy: not a re-derived band, but a **finer tolerance**.

## 5. The fix, and its cost

`tolerance` is a setting, so this needs no new algorithm:

```text
tolerance   bisections   lattice spacing        improvement
1e-3            10       6.7713e-04 = 0.0677 %     1x   (current)
3e-4            12       1.6924e-04 = 0.0169 %     4x
1e-4            13       8.4616e-05 = 0.0085 %     8x
1e-5            17       5.2883e-06 = 0.0005 %   128x
```

Each extra bisection is one more trial march. **`tolerance = 1e-4` costs 3 extra trials and buys 8x
resolution**, which would put C6a's fine difference at ~16 steps instead of 2 and make its order
determinate.

**This is a recommendation, not a change.** Tightening `tolerance` alters every recorded `S_L`
(they would move off the current lattice onto a finer one), so it invalidates comparisons with
every number in the register and needs its own authorization, gate and two-library attribution.

## 6. What is NOT established

```text
NOT established  that r0 = 2 ALWAYS. §1 reads it from the doubling/halving at :1821, and every
                 default-seed run agrees -- but a seed that brackets on the first try, or
                 `maximumBracketSteps` binding, could give a different r0 and hence a different
                 spacing. The two off-lattice C6 values show the anchor already varies; whether the
                 RATIO can is unchecked.
NOT established  the effect on quantities OTHER than S_L. The fuel balance, T_peak and x_front are
                 not bisection outputs, so they are not on this lattice -- but they are computed
                 from a quantized mdot, so they inherit a quantized INPUT. That coupling is not
                 characterised.
NOT established  that tightening tolerance is safe. §5 gives the arithmetic, not a verification.
                 More bisections mean more trials, each of which can hit `maximumStepsPerTrial`,
                 and TD-031's sawtooth means a trial's verdict depends on where its march stops.
NOT measured     whether G8's own 35.0-37.0 cm/s criterion is affected. At 0.0677 % the lattice is
                 far below that 5.6 % band, so it is almost certainly immaterial there -- but
                 "almost certainly" is not a measurement, and TD-006 should state it explicitly.
NOT a change     nothing in production was touched.
```

**§6's second item is the one that could still bite.** `S_L` is on the lattice; everything the
solver reports downstream of `mdot` is computed from a lattice point. TD-004's C6a balance clause,
TD-005's reaction integral and TD-030's excess are all such quantities, and none has been checked
for inherited quantization.
