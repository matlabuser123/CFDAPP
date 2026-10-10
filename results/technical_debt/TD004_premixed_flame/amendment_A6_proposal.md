# TD-004 C6a — §5.3 derivation. The order clause fails BECAUSE THE SOLVER GOT BETTER.

```text
STATUS:      §5.3 steps 2-3 COMPLETE (investigate, derive). Step 4 is REQUEST AUTHORIZATION, which
             this document does. NOTHING IS FROZEN and no threshold is changed.
SCOPE:       C6a's two failing clauses. C6, C5/GATE_A3, C1-C4 untouched.
EVIDENCE:    logs/A5_01_c6_c6a.log (post-fix) · logs/08_fixture_selection.log (pre-fix)
             TD043_eigenvalue_lattice/audit.md (the lattice)
             logs/A6_01_species_suite_regression.log (C7's question, answered separately)
```

---

## 1. The balance clause — a clean, derived replacement

C6a requires `fuel balance < 0.02` at every level of `{125, 250, 500}`.

```text
                  125        250        500
PRE-FIX        0.4703     0.5457     0.0433      all three FAIL -> broken criterion (§5.2)
POST-FIX       0.1505     0.0283     0.0063      two FAIL
```

**The quantity is a discretisation error.** Measured convergence on the fixed tree:

```text
0.1505 / 0.0283 = 5.318  ->  order 2.411
0.0283 / 0.0063 = 4.492  ->  order 2.167
```

So the balance falls slightly faster than `h^2`. `CLAUDE.md` §5.1 is explicit: *"If the quantity
scales with 1/h or a coordinate ratio, the threshold must scale too."* An absolute `0.02` applied
across a 4x mesh range is dimensionally wrong, which is why the baseline fails it everywhere.

### 1.1 The derived replacement

```text
fuel balance(cells)  <  0.02 * (500 / cells)^2
```

`h^2` is chosen rather than the measured 2.2-2.4 deliberately: it is the scheme's formal order and
therefore **conservative** — the bound is looser than the observed convergence, so it cannot pass a
solver whose balance converges more slowly than second order.

```text
cells   bound    POST-FIX            PRE-FIX
  125   0.3200   0.1505  PASS        0.4703  FAIL
  250   0.0800   0.0283  PASS        0.5457  FAIL
  500   0.0200   0.0063  PASS        0.0433  FAIL
```

**The 500-cell bound is unchanged at `0.02`** — the value C3 calls *"the existing test threshold,
neither weakened nor tightened"* — so the reference configuration is gated exactly as before. The
baseline fails all three levels, so the criterion is non-vacuous and still detects the pre-fix
defect. This is a dimensional correction, not a relaxation.

## 2. The order clause — and this is NOT what either of my earlier readings said

Against TD-043's lattice (relative spacing `0.067713 %`), with the uncertainty from ±0.5 step per
reported value:

```text
                 diffs (cm/s)          in quanta      p       interval        width   verdict
PRE-FIX     3.6651e-01  8.8060e-02     113.4  27.2   2.057   (1.99, 2.12)     0.13    INSIDE, determined
POST-FIX    4.3360e-02  6.7000e-03      12.9   2.0   2.694   (1.99, 3.80)     1.81    UNDETERMINED
```

**Pre-fix the order was well determined and comfortably in band. Post-fix it is unresolvable — and
the cause is that the fix shrank the successive differences by about 8x.**

The levels now agree far better than they did. `S_L` at 125/250/500 went from
`4.40773 / 4.77424 / 4.86230` to `4.90527 / 4.94863 / 4.95533`, and the gaps between them collapsed
onto the lattice floor. **C6a's order clause fails because the solver improved**, not because it
regressed. Neither §2.2 (a regression) nor §2.2a (resolution-limited, remedy = finer tolerance) was
the whole story.

### 2.1 And a finer tolerance does NOT rescue it

At `tolerance = 1e-4` (8x finer, 3 extra bisections) the differences become ~103 and ~16 quanta:

```text
interval (2.59, 2.80)  ->  DETERMINED, and OUTSIDE the [1.5, 2.5] band
```

So tightening the tolerance would **confirm** `p ~ 2.7` rather than bring it into band. §2.2a's
suggested remedy is therefore wrong too, and I am withdrawing it: a finer tolerance makes the
failure *definite* instead of *undetermined*.

### 2.2 What `p ~ 2.7` probably means, and why I am not proposing a band for it

An observed order above the formal order is the signature of error cancellation or a
non-asymptotic family. Both are live here:

```text
TD-029   the family couples dt ~ dx^2, and the spatial and temporal errors have OPPOSITE signs.
         Pre-fix the temporal error was 4.24 % and comparable to the spatial; post-fix it collapsed
         to <= 0.135 %. So the error BALANCE changed completely, which is exactly the kind of change
         that moves an observed order.
fixture_probe's own header, written before any of this:
         "A coarser triplet is LESS likely to be asymptotic, so moving the family downward is
          exactly the change that can invalidate the order silently."
```

**C6a was written to ask whether the cheap triplet is still asymptotic. The answer appears to be
no — and that is a finding about the FIXTURE, not about the locator fix.** The record's `p = 1.93`
came from the EXPENSIVE `{250, 500, 1000}` triplet, which C6a replaced because it does not finish in
the cap.

**I am therefore NOT proposing a re-derived band.** Deriving a band around a measured 2.7 would be
fitting a threshold to an observation, which §5.3 forbids outright. What the band should be for this
family cannot be settled without knowing the family's true order, and that needs a resolved
measurement the current tolerance cannot give.

### 2.3 What I AM proposing: an explicit resolution precondition

```text
C6a-order, amended:
  An observed order may be CLAIMED only if both successive differences exceed 4 lattice quanta,
  where the quantum is 2^(1/1024) - 1 of S_L (TD-043). Below that, the order is UNRESOLVED and the
  criterion is RECORDED AS UNRESOLVED -- which is a STOP, not a PASS.
  When resolved, the order's full uncertainty interval must lie within [1.5, 2.5].
```

`4 quanta` is derived, not picked: `dp ~ (1/ln2)(1/n1 + 1/n2)`, so for the interval to be narrower
than `0.5` with `n1 ~ 13` one needs `n2 >= 4`. Requiring the full interval to lie inside the band is
**stricter** than the point-estimate test it replaces — it makes the criterion prove the order is in
band rather than merely estimate it there.

Applied now: `n2 = 2.0 < 4`, so **C6a-order is UNRESOLVED** and the gate stops there with that
recorded. It does not become a pass.

## 3. C7's question, answered separately and NOT as a gate verdict

The frozen order stops at C6a, so C7 was not evaluated. But *"did the locator fix silently change
anything else?"* had never been answered, and it is cheap:

```text
CFDSpeciesTests excluding G8PremixedFlameTest:   343 tests, 343 PASSED, 14 863 ms
C7's recorded baseline:                          343
```

Exact match. **No silent change elsewhere in the species suite.** Recorded as an independent
regression check in `logs/A6_01`, explicitly not as a C7 verdict.

## 4. THE AUTHORIZATION REQUESTED, and the three ways forward

§5.3 step 4. Nothing below is done without an explicit decision.

```text
the BALANCE clause   §1.1 is a straightforward dimensional correction: derived from the scheme's
                     formal order, the baseline fails all three levels, the 500-cell bound is
                     unchanged. I recommend freezing it as GATE_A6.
the ORDER clause     §2.3 adds a resolution precondition and a stricter in-band test. It leaves
                     C6a UNRESOLVED and the gate stopped -- it does not unblock anything.
```

The real decision is what to do about the order, and the options are genuinely different in kind:

```text
(A) freeze §1.1 + §2.3. Honest and minimal. C6a stays stopped, recorded as "balance PASS, order
    UNRESOLVED". C6b and C7 remain unevaluated. Costs one dry-run; no long solves.
(B) authorize TD-043's tolerance change FIRST (1e-3 -> 1e-4), re-measure, then decide the band on a
    resolved order. This is the only route that ever answers "is p ~ 2.7 real?" -- but it moves
    EVERY recorded S_L onto a new lattice and so needs its own gate and two-library attribution.
(C) restore the EXPENSIVE {250, 500, 1000} triplet and accept the cost. It is the family the
    p = 1.93 record came from, so it is the only one whose band is not borrowed. logs/07 records
    that it does not finish in 1750 s; the per-test cap is 7200 s, so whether it fits has never
    actually been measured at the cap -- only at 1750 s.
```

**(C) deserves attention it has not had.** The cheap triplet exists because the expensive one
"does not finish in 1750 s" — but the real cap is `ctest --timeout 7200`, and the 1000-cell level
was never run against *that*. Given 500 cells now costs ~940 s and cost grows ~cells^2.2, 1000 cells
projects to roughly 4 300 s, which is **inside** 7200 s. If that holds, the fixture never needed
shrinking and C6a would not exist.

## 4a. OPTION (C) IS MEASURED AND DEAD — the 1000-cell level does not fit the cap

`logs/A6_02_thousand_cell.log`, run under `timeout 7200` with the exit code captured **before any
pipe**:

```text
HARNESS EXIT CODE (unmasked): 124
  124 = KILLED BY timeout AT 7200 s
```

So the expensive `{250, 500, 1000}` triplet is genuinely unaffordable **at the real per-test cap**,
not merely at the 1750 s `logs/07` used. §4(C)'s projection of "roughly 4 300 s" was wrong — it
extrapolated the TD-035 ladder's cost exponent, whose own interval fits ranged 0.79 to 4.07, and
the true cost lands on the far side of the cap.

**Two things this run fixed about the record, independent of its verdict:**

```text
logs/07 recorded `exit 0` for a run it described as timing out. That was a `timeout ... | tee`
pipeline reporting tee's status. The one number meant to prove a timeout proved nothing. This run
captures the exit code before the pipe, so 124 means 124.

logs/07 ruled the level out at 1750 s, which is NOT the repository's cap. It has now been tested
against 7200 s.
```

**What this run did NOT establish**, because the solve never completed:

```text
NOT measured  S_L at 1000 cells, the flame thickness guard, the fuel balance, or the
              {250, 500, 1000} order. The probe printed its header and was killed mid-solve.
NOT measured  whether the trial cap binds at 1000 cells. The `settled` check never ran, so
              whether fixture_probe's 20000 would have truncated this level is still UNKNOWN --
              and so is whether logs/07 was measuring cost or measuring its own cap.
NOT measured  BY HOW MUCH it exceeds 7200 s. Only that it does.
```

**C6a therefore stays**, and the amendment question in §4 reverts to options (A), (B) and
balance-only. The cheap triplet is not a convenience — it is forced.

## 5. What is NOT established

```text
NOT established  that p ~ 2.7 is the family's true order. The point estimate says so and §2.1 says
                 a finer tolerance would confirm it, but neither is a resolved measurement.
NOT established  that the cheap triplet is non-asymptotic. §2.2 gives two mechanisms that would
                 produce an inflated order; it does not demonstrate either.
NOT established  that the 1000-cell level fits the 7200 s cap. §4(C)'s projection extrapolates a
                 cost exponent from the TD-035 ladder, whose own fits ranged 0.79 to 4.07 -- so
                 "roughly 4 300 s" is a projection, not a budget. It would have to be measured.
NOT measured     C6b. It needs S_c at 125/250/500, which fixture_probe does not report.
NOT frozen       anything. No gate file is written and no threshold is changed by this document.
```
