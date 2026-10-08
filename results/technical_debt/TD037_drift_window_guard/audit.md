# TD-037 — `driftWindow` is unguarded against trial length

```text
opened      2026-10-07, carved out of TD-032
authorized  2026-10-07, in full
status      AUDIT. No production change yet. No gate frozen.
severity    P1. It carries the G8 method-insensitivity failure TD-032 exposed, and it makes a
            documented, test-varied setting able to silently destroy a measurement.
reproducer  results/technical_debt/TD032_lagged_time_level/logs/08_why_noflame.log — already
            recorded, no new run needed to see the defect.
```

---

## 1. The defect, exactly

A trial's drift is read from a sliding window over the front's position history. There are **two**
reading sites and they do **not** share a condition:

```text
src/species/PremixedFlame.cpp
  1344   const Index window = std::max<Index>(4, settings.driftWindow);

  1432   NORMAL path       history.size() > window  &&  (step % window) == window - 1
         -> a drift is read only AT A WINDOW BOUNDARY, i.e. first at step `window - 1`

  1384   EXTINCTION path   history.size() > window
         -> no boundary requirement, but still MORE THAN `window` positions of history
```

**A trial that extinguishes before accumulating more than `driftWindow` positions is therefore
unsidable**, and `trialSaysTooLarge()` then returns false, and the search aborts.

Measured on one library, one mass flux, one trial — only the window differs:

```text
driftWindow 32   trial 3   mdot 0.0870611   steps 40   extinguished YES   measured YES   sided
driftWindow 64   trial 3   mdot 0.0870611   steps 40   extinguished YES   measured NO    UNSIDED
                 -> Converged, 15 trials, bracketSteps 4
                                                 -> NoFlame, 4 trials, bracketSteps 0
```

`40 > 32` and `40 < 64`. That is the whole mechanism.

## 2. What is NOT wrong, and must not be "fixed"

**`trialSaysTooLarge()` fails CLOSED and that is correct.** Its comment says so: *"a trial that
never measured a drift returns false and the search reports it rather than inventing a side."* An
earlier version inferred the side from accumulated recentring and defaulted to "downstream", which
read every ambiguous trial as *too large* and halved the mass flux thirteen times without
bracketing anything. **Do not make the unsided case guess.** The defect is upstream of it: a
legitimate setting makes a measurable trial unmeasurable.

## 3. The guard that exists in spirit but not in code

`validateSettings` checks only that the value is non-zero — and its message shows the author had
already identified this exact failure mode:

```text
  158   if (settings.driftWindow == 0 || settings.maximumStepsPerTrial == 0 || ...)
          "driftWindow, settledWindowsRequired and maximumStepsPerTrial must all be >= 1;
           A TRIAL THAT CANNOT MEASURE A WINDOW CANNOT DECIDE A SIDE"
```

The reasoning is right and the guard covers only the degenerate case. Nothing anywhere relates
`driftWindow` to `maximumStepsPerTrial`, to `minimumStepsPerTrial`, or to how long trials actually
survive — and trial lifetime is not knowable in advance, which is why a static validation check
cannot be the whole answer.

## 4. Why this is worse than a tuning wart

`driftWindow` is **documented, defaulted, and deliberately varied by a test**.
`G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial` sweeps `{16, 32, 64}` precisely to
establish that the answer does not depend on this knob — which is the right thing for a test to do,
since a trial too short to let the structure respond would read the transient rather than the
propagation. **The test is correct and the solver is not.** At 64 the solver does not return a
different answer; it returns no answer, under a status that claims nothing is burning (TD-036).

So the knob the method-insensitivity test exists to vary is the knob that can silently abort the
search.

## 5. Candidate fix, and the floor is derived from the code's own standard

On the extinction path, read the drift over whatever history exists rather than refusing:

```text
effective window = min( driftWindow, history.size() - 1 )   floored at 4
```

**Why 4 and not an invented constant:** line 1344 already applies `std::max<Index>(4, driftWindow)`,
so a 4-position window is what this code *already treats as the smallest meaningful one*. Using the
same floor on the extinction path is consistency, not a new tolerance.

What this would have done to the measured case: trial 3 has ~40 positions, so at window 64 the
effective window becomes 39 and the trial is sided — as it already is at window 32.

**Alternatives considered and why they are worse:**

```text
lower the default window     does not fix it. The test varies the knob on purpose, so any fixed
                             default can be exceeded by the next configuration.
validate driftWindow against does not fix it. Trial lifetime is not known before the trial runs;
maximumStepsPerTrial         extinction is an OUTCOME, not a setting.
make the unsided case guess  FORBIDDEN -- see section 2. That is the fail-open defect this code
                             already corrected once.
refuse the configuration     honest but useless: it would make the method-insensitivity test
                             unrunnable at the window it exists to test.
```

## 6. Open questions the gate must answer, not assume

```text
a  does the drift measured over a SHORT window mean the same thing? A 4-position drift on an
   extinguishing flame may be dominated by the extinction transient rather than propagation.
   If it is, the side it assigns could be WRONG, which is worse than undecided -- and that
   possibility is why section 2 forbids guessing.
b  does the fix change any currently-passing answer? S_L comes from the trials' drift SIGNS, so
   admitting previously-unsided trials can change the bracketing path and therefore S_L.
   This must be measured as bitwise-or-bounded, not assumed.
c  is `driftWindow` reachable from a case file at all, or is this library-only? That sets the
   production severity and is not yet established.
```

**(b) is the one that could stop this item.** A fix that changes `S_L` at the gated fixture would be
trading a hard failure for a silent change of answer, and the gate must bound it before the fix is
accepted.

## 7. Evidence inherited from TD-032

```text
logs/08_why_noflame.log    the per-trial record on the FIXED library -- the measurement in §1
logs/07_window64_probe.log the field-level probe, and the record of its own wrong auto-verdict
logs/09_..._prefix.log     the same per-trial record on the PRE-FIX library -- LANDED, see §8
```

## 8. ATTRIBUTION COMPLETE — the boundary crossing is now measured, not inferred

`TD032_lagged_time_level/logs/09_why_noflame_prefix.log`, pre-fix library `3b94eb849f9c8277…`
(byte-identical to TD-032's frozen lagged library), source proved `dac2992006a51cce…`:

```text
driftWindow 64, THE SAME TRIAL at mdot 0.0870611, extinguished in both cases:

   pre-fix    71 steps   drift  0.104453   measured YES   sided tooLarge   -> Converged, 15 trials
   post-fix   40 steps   drift  0          measured NO    UNSIDED          -> NoFlame,     4 trials
```

**71 > 64 and 40 < 64.** TD-032's time-level fix shortened that trial's life from 71 steps to 40,
which carried it across the `history.size() > driftWindow` boundary at window 64. Pre-fix the
condition held with 7 steps of margin; post-fix it fails by 24.

This closes the one link TD-032's closure left open, and it lands exactly where the carve-out put
it: **the boundary is pre-existing and is this item's to fix; being on the wrong side of it is what
TD-032 changed.** Seven steps of margin on an uncontrolled quantity was never a guarantee — the
defect was latent, not absent.

It also sharpens §6(a). The pre-fix trial read a drift of `0.104453` over a 64-position window at
71 steps of life; the post-fix trial would have to be read over at most 39 positions. The question
of whether a short-window drift on an extinguishing flame measures propagation or the extinction
transient is therefore **live and quantitative**, not hypothetical, and C3 exists to answer it.
