# TD-028 — RESOLVED on its own observable. The swing fell by a factor of 10 million.

```text
STATUS:      RESOLVED by TD-032, verified on TD-028's OWN observable (the field, not S_L).
             The period-14 characterisation and BOTH dt attributions remain INVALID AS
             AUTHORITATIVE -- resolving the alternation does not restore what it contaminated.
SCOPE:       the period-2 field alternation only. No production change.
RESULT:      T[35] swing 1.4836e+03 K -> 1.438470e-04 K, a factor of 1.031e+07
EVIDENCE:    logs/01_cell35_remeasure.log
             baseline: TD031_final_march_length/logs/05_cell35_history.log (pre-TD-032)
INSTRUMENT:  tools/cell35_remeasure_probe.cpp. TD-031's original probe is UNTOUCHED at its
             recorded hash 620013f0c92ff086… -- see §3 for why a new one was needed.
TREE:        PremixedFlame.cpp fa815c97… · libcfdcore.a 852555cf… (gates asserted in the log)
COMMIT:      none — not authorized.
```

---

## 1. The measurement, against the recorded baseline

500 cells / cap 5 / relaxT 18, the march stopped at five consecutive budgets via `finalMarchSteps`.
Cell 35 is `x = 0.70 mm`, in the cold preheat zone.

```text
PRE-FIX (TD031 logs/05)                     POST-FIX (this run)
budget    T[35] K          dT               budget    marchStp   T[35] K            dT
 30945     312.99462    0.0000e+00           27507      27507     310.36007780984   0.0000e+00
 30946    1796.61325   +1.4836e+03           27508      27508     310.36011379303  +3.5983e-05
 30947     312.99216   -1.4836e+03           27509      27509     310.36014976697  +3.5974e-05
 30948    1796.60836   +1.4836e+03           27510      27510     310.36018570228  +3.5935e-05
 30949     312.98969   -1.4836e+03           27511      27511     310.36022165688  +3.5955e-05
```

```text
swing            1.4836e+03 K  ->  1.438470e-04 K        factor 1.031e+07
as % full rise          98.9 %  ->  9.5898e-06 %
worst cell 25..45            —  ->  cell 45, 2.169e-04 K
```

**Every one of the five marches stopped exactly where it was told** (`marchStp == budget` on all five
rows), so these are genuinely consecutive states and the clip-failure mode the probe guards against
did not occur.

## 2. It is not merely smaller — the SHAPE changed

The four post-fix increments are `+3.5983e-05, +3.5974e-05, +3.5935e-05, +3.5955e-05`:
**monotone, same sign, equal to three digits.** That is a steady drift, not an oscillation. The
pre-fix sequence alternated sign on every step with a constant magnitude — the period-2 signature.

So this is not an alternation damped by seven orders of magnitude; it is a different behaviour
entirely. **The two interleaved sub-sequences no longer exist.**

### 2.1 The residual drift is TD-031's sawtooth, independently visible

`~3.6e-05 K/step` at a fixed cell in the preheat zone is what a front translating upstream looks
like from a stationary observer: the preheat profile slides past cell 35 and its temperature creeps
up. TD-031 measured that drift directly as `1.1-1.3e-04 cells/step`.

Consistent with it, and worth stating as corroboration rather than as a new result: two probes
written for different items, reading different observables, agree that **the front still moves while
the field no longer alternates.**

### 2.2 `worstStateChange` also fell, and what remains is the drift

```text
pre-fix   9.8957e-01   saturated -- 98.96 % of the full rise per step
post-fix  5.0723e-02   constant across all five budgets
```

A 19.5x fall, but `5.07e-02` is still 5 % of the full rise per step, and it is **identical at every
budget**. That is the front's motion, not alternation — and it is why `steadyStateReached` is still
false. TD-031 owns it.

## 3. Why a new probe was needed, and why that matters

TD-031's original probe hardcodes `budgets[] = {30945, 30946, 30947, 30948, 30949}` and relies on

```text
finalBudget = min( finalMarchSteps, derived_bound )
```

to clip the march. Pre-fix the derived bound at relaxT 18 was `30949`, so those budgets clipped.
**On the fixed tree the bound is `27511`** (TD-027's C4 measured it), so all five original budgets
now EXCEED it, every run would clip to the same `27511`, and the probe would print **five identical
states with zero variation** — which reads exactly like "the alternation is gone".

**That is an instrument confirming a hypothesis by failing to vary its own independent variable.**
It was caught by reading the probe before running it, not by a suspicious result. The replacement
uses budgets below the bound and asserts `marchStp == budget` per row, voiding the run on any
mismatch rather than averaging it into a verdict.

TD-031's probe is left at its recorded hash so its own evidence stays reproducible.

## 4. The prediction

`cell35_remeasure_probe.cpp` registered, before the run: *"T[35] varies smoothly and by much less
than 1 K, because the transient path that created the two interleaved sub-sequences no longer
exists."* Measured `1.44e-04 K`, smoothly. **Correct**, and for the stated reason this time.

## 5. WHAT RESOLVING TD-028 DOES NOT RESTORE

```text
STILL INVALID  the period-14 limit-cycle characterisation. It was measured on the alternating
               field and is marked INVALID AS AUTHORITATIVE. This run shows the alternation is
               gone; it does NOT re-derive the limit cycle, and TD-031's sawtooth suggests the
               right characterisation may not be a 14-step cycle at all.
STILL INVALID  BOTH dt attributions (TD027_cell_diffusion_cap/attribution_correction.md). Same
               reason. They need re-measuring on the fixed tree, and TD-043's lattice now bounds
               how finely any such attribution can be resolved.
```

**Clearing a root cause does not re-validate the measurements it corrupted.** TD-028's register
entry names three things — the alternation, the period-14 characterisation, and the attributions.
This run settles the first.

## 6. What is NOT established

```text
NOT established  that the alternation is gone at OTHER fixtures. This is 500 cells / relaxT 18,
                 the configuration TD-031 localised it at. 63/125/250 cells and other relaxT are
                 not re-measured -- though TD-027's C4 found S_L parity-independent across four
                 budgets, which is indirect support at this fixture.
NOT established  that no OTHER cell alternates. Cells 25-45 were checked (worst spread 2.17e-04 K
                 at cell 45); the other 479 cells were not.
NOT established  the residual 3.6e-05 K/step drift's law. §2.1 attributes it to TD-031's front
                 translation on the strength of agreement in order of magnitude and direction.
                 Five points over four steps cannot establish a rate law.
NOT re-measured  the period-14 cycle or either dt attribution. §5.
NOT run          any regression. No production change.
```
