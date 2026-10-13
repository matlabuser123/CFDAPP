# TD-003 C3 — the `1e-8` bound was NEVER reachable, is in NO frozen gate, and was never derived. The project recorded this once already.

```text
STATUS:      **ANSWERED: NO.** `1e-8` was not reachable when it was written and has never been
             met. **0 of 81 recorded `|sum Y - 1|` measurements across the whole campaign fall
             below it** -- the range is `9.54994e-08 .. 4.00526e-06`, so the closest any
             configuration ever came was 9.5x over and the worst was 401x over.
             **It is in NO frozen gate.** `results/p18/gates/P18_GATES.md` contains no such
             bound. It is a bare assertion in a test file that was untracked until 1a70f0a.
             **It was never derived.** Every neighbouring bound on this same quantity class is
             a DERIVED RELATIVE round-off bound; `1e-8` is the only absolute constant among
             them, and the SAME constant is applied to a second, unrelated invariant where it
             passes with 529x margin.
             **And the project already knew.** TD-004's own frozen `GATE.md` section 3.2 records
             in writing that the test was "already failing, before TD-004 changes anything" and
             deliberately gated against a measured band instead.
             So this is NOT a threshold that went stale. It never held.
SCOPE:       provenance only. No production change, no test change, no threshold change. The
             `1e-8` still stands untouched and the test still fails.
INSTRUMENT:  a grep over `results/technical_debt/` for every recorded `|sumY-1|`, plus reading
             the frozen gate files. No run.
EVIDENCE:    this file; the counts are reproducible by the command in section 1.1.
COMMIT:      carried as evidence.
```

---

## 1. IT WAS NEVER MET

```text
recorded |sum Y - 1| measurements found      81
minimum                                      9.54994e-08      9.5x over the bound
maximum                                      4.00526e-06    401x over the bound
measurements below 1e-8                      0 of 81
```

### 1.1 Reproduction

```bash
grep -rhoE '\|sumY-1\|[ =]+[0-9.e+-]+' results/technical_debt/ \
  | grep -oE '[0-9.]+e[+-][0-9]+' | sort -g
```

## 2. IT IS IN NO FROZEN GATE

```text
results/p18/gates/P18_GATES.md        no 1e-8 bound on any quantity -- searched
G3 (species conservation)             |imbalance| / |total species mass| <= 8 * eps * N_cells
G2(a) (MassFractionState)             4 * eps * N_species, named "the frozen G2(a) tolerance"
section 30 (diffusive closure)        8 * eps * N_species * max_f max_i |J^0_{i,f}|
the 1e-8                              appears ONLY at tests/unit/species/test_premixed_flame
                                      .cpp:471, with no derivation and no gate behind it
```

The test file carrying it was **untracked until commit `1a70f0a`** (this session's production
commit), so there is no version history that could date or justify the constant.

## 3. IT WAS NEVER DERIVED — three independent signs

### 3.1 Every neighbour is relative; this one is absolute

```text
G2(a)   4 * eps * N_species             = 1.776e-15   (N = 2)
G3      8 * eps * N_cells               = 8.882e-13   (N = 500)
sec 30  8 * eps * N_species * max|J^0|  = relative, scaled by the flux actually present
test    1e-8                            = 1.000e-08   ABSOLUTE CONSTANT
```

It is **4 to 7 orders looser** than the derived bounds on the same quantity class — so it was
not derived tightly, and not derived loosely either. It was not derived.

`SpeciesTransport.hpp:149` states the reasoning the `1e-8` violates, about its own sibling
quantity:

> *"A RELATIVE bound scaled by the largest raw flux actually present, because an absolute
> tolerance on a dimensional flux is meaningless across meshes and diffusivities."*

### 3.2 The same constant is used for two unrelated invariants

`test_premixed_flame.cpp:470`-`:471`:

```text
EXPECT_GT(flame.minMassFraction, -1.0e-8)          measured -1.889798e-11   PASSES, margin 529x
EXPECT_LT(flame.maxCompositionSumDeviation, 1e-8)  measured  8.822866e-07   FAILS by 88x
```

One constant, two quantities with different dimensions and different floors, 529x margin on one
and an 88x violation on the other. **That is the signature of a constant chosen to mean "small",
not a floor derived per quantity** — which is exactly what CLAUDE.md section 5.1 requires
("Name the exact quantity each threshold bounds. Derive its floor from that quantity's
dimensional, round-off and discretization behaviour").

### 3.3 The test's own comment says so

> *"The two invariants the iterate cannot hold to a state boundary's tolerance stay negligible."*

"Stay negligible" is a smallness statement. The "state boundary's tolerance" it contrasts against
is G2(a)'s `4 * eps * N_species = 1.776e-15`, which IS derived — so the author knew the derived
bound, knew the iterate could not meet it, and reached for a round number instead of deriving the
iterate's own floor.

## 4. THE PROJECT ALREADY RECORDED THIS, AND WORKED AROUND IT

`results/technical_debt/TD004_premixed_flame/GATE.md` section 3.2, frozen before TD-004's work:

> *"The existing test asserts `maxCompositionSumDeviation < 1e-8` and the baseline measures
> `6.29e-07 .. 1.10e-06` — so that test is **already failing**, before TD-004 changes anything"*

and it therefore gated `|sum Y - 1|` against **the measured baseline band `1.2e-06`** rather than
the `1e-8`. So an earlier frozen gate in this very campaign reached the same conclusion about
reachability and routed around it.

### 4.1 But its stated CAUSE was wrong, and that matters

The same section attributes the failure to *"the tolerance-limited solve ... which is TD-003's
territory"*, and says *"logs/03 shows the value creeping with march length"*. Both are now
refuted by measurement:

```text
"tolerance-limited"        the linear solve contributes 1.04 %; an exact solve leaves 99.97 %
                           (verification_closure_test.md)
"creeping with march"      steps rise 2.89x while the deviation FALLS 0.57x -- wrong sign
                           (verification_sigmay_sources.md section 3.2)
```

**So TD-004 section 3.2 was right about the FACT and wrong about the CAUSE** — and the wrong cause
is what kept this filed under a linear-solver item for the whole campaign.

## 5. THE DERIVED FLOOR EXPLAINS THE HISTORY — with one honest wrinkle

```text
derived floor  eps * ||b|| * amplification     6.503e-07
TD-004's recorded band                         6.29e-07 .. 1.10e-06
the full 81-measurement range                  9.54994e-08 .. 4.00526e-06
```

TD-004's band sits squarely at the derived floor, which is the consistency check: the floor has
governed this quantity all along, under a bound that never acknowledged it.

**The wrinkle is the `9.54994e-08` minimum, which is 6.8x BELOW the floor I derived.** That is not
a contradiction — the floor scales with `||b||` and with the operator's amplification, both of
which are state-dependent, so a different configuration has a different floor. But it carries a
consequence worth stating plainly:

```text
the floor is NOT immovable. Some recorded configuration reached 9.55e-08, within 9.5x of the
1e-8 bound. So an amplification roughly 10x smaller would put the bound within reach, which
means OPTION B (reduce the amplification rather than amend the bound) is NOT futile.
```

I have not identified which configuration produced `9.55e-08` or why. That is the measurement
that would decide between Options A and B, and it has not been made.

## 6. WHAT THIS CHANGES ABOUT THE DECISION

```text
BEFORE   "the 1e-8 may be stale -- derived against a different mesh, dt or ||b||, reasonable
         then and wrong now." A routine amendment.
NOW      the bound is in no frozen gate, was never derived, has never been met in 81
         measurements, and an earlier frozen gate in this campaign already recorded it as
         unreachable and routed around it.
```

So a section 5.3 amendment here is **not correcting a previously-correct threshold**. It is
replacing an underived constant with a derived bound, in a form this repository already uses three
times for the same quantity class. That is a cleaner case for Option A than I had before — and
section 5's wrinkle is the one argument for Option B, which is unmeasured.

**The decision remains the user's.** Section 5.3 step 4 is unchanged: nothing was amended.

## 7. WHAT IS NOT ESTABLISHED

```text
NOT established  which configuration produced the 9.54994e-08 minimum, or why it is 6.8x below
                 the floor derived for the gated state. Section 5. This is the measurement that
                 would decide A vs B.
NOT established  that no 1e-8 derivation exists outside results/technical_debt/ and
                 results/p18/gates/. Those are where a derivation would live and neither has
                 one; earlier phases were not exhaustively searched.
NOT established  that the 81 measurements are independent. Many are repeated sweeps of the same
                 fixture, so "0 of 81" is a statement about recorded history, not about 81
                 distinct physical states.
NOT established  the correct replacement constant. Section 5.3 forbids deriving it from observed
                 output, so an amendment must derive it from the number of summed terms in the
                 assembly and the operator's conditioning, then dry-run it.
NOT changed      the 1e-8 threshold, the test, or any production file.
```
