# TD-031 — CONFIRMED, and its character changed: the front does not converge, it SAWTOOTHS.

```text
STATUS:      RE-MEASURED on the TD-032-fixed march. TD-031's claim is CONFIRMED and STRENGTHENED:
             the front is still translating upstream, and it never settles -- the recentring
             controller resets it, so the march reaches a BOUNDED LIMIT CYCLE, not a steady state.
SCOPE:       the front's dependence on march length. No production change.
CAVEAT:      this trajectory is ASSEMBLED from runs made for other criteria, not from a dedicated
             budget ladder. §4 states what that costs.
EVIDENCE:    TD027_cell_diffusion_cap/logs/A2_01_c4_parity_remeasure.log  (4 points)
             TD004_premixed_flame/logs/A3_04_verification.log             (1 point)
             TD029_error_cancellation/logs/01_dt_dx_separation.log        (leg A, same point)
TREE:        PremixedFlame.cpp fa815c97… · libcfdcore.a 852555cf…
COMMIT:      none — not authorized.
```

---

## 1. The front against march length, on one fixture

All five points are 500 cells, `dt = 9.4243e-06`, `anchorFraction 0.35`, `driftWindow 32`,
`maximumStepsPerTrial 80000` — a single fixture, varied only in final-march budget:

```text
budget   front mm   off anchor (cells)   source
 15283   3.499220              -0.039   A3_04 / TD-029 leg A
 27511   3.468162              -1.592   TD-027 C4, relaxT 18
 33624   3.453799              -2.310   TD-027 C4, relaxT 22
 39738   3.440195              -2.990   TD-027 C4, relaxT 26
 45851   3.485637              -0.718   TD-027 C4, relaxT 30
```

## 2. The shape is a SAWTOOTH, and that is the finding

```text
 15283   -0.039  \
 27511   -1.592   |  drifting UPSTREAM, monotonically
 33624   -2.310   |
 39738   -2.990  /   <- at the dead band: recentreDeadBandCells = 3.0
 45851   -0.718      <- RESET. A 3-cell shift fired, then drift resumed.
```

`-2.990` cells sits **0.010 cells** from the `3.0`-cell dead band. By 45 851 steps the band has been
crossed, `recentre()` has applied its 3-cell shift (`-2.990 + 3 = +0.010`), and the front has drifted
back to `-0.718`. **The front never stops moving. It is held in a bounded cycle of amplitude 3 cells
= 0.06 mm by the controller.**

The drift rate, from the four monotone points:

```text
15283 -> 27511   -1.270e-04 cells/step
27511 -> 33624   -1.174e-04 cells/step
33624 -> 39738   -1.112e-04 cells/step
```

**Mildly decreasing, 1.27 -> 1.11e-04.** Four points cannot distinguish "linear drift plus noise"
from "very slow convergence", and no rate law is claimed from them.

## 3. TD-031's claim, re-judged

```text
RECORDED   "the FRONT is still translating upstream after 51 583 steps (decrements halving,
            0.328 -> 0.017 mm), so a march-length criterion must watch the FRONT and not only the
            per-step field change"

CONFIRMED  the front is still translating upstream on the fixed march.
CHANGED    the character. "Decrements halving" reads as a sequence CONVERGING to a limit. What is
           measured now is a steady drift terminated by a controller RESET -- which does not
           converge at all. A criterion waiting for the front to settle would wait forever.
STRENGTHENED  it is not that relaxT = 10 reports a transient while some larger relaxT would not.
           NO relaxT reports a settled front, because the front's motion is periodic, not decaying.
```

This matches what the solver itself reports: every run in C6's test output ends
`final march 15282 (NOT steady)`. `steadyStateReached` is **false**, and it is false for a structural
reason rather than an insufficient budget.

### 3.1 Two other results this explains

**The fuel balance's dependence on relaxT.** TD-027's C4 family gave
`6.139 / 6.062 / 5.986 / 6.235e-03`. Non-monotone, and the maximum is at relaxT 30 — the level that
had just recentred. The balance depends on **where in the sawtooth the march stops**, which is why
its spread is small but not vanishing.

**C5c's bound was right for the right reason.** GATE_A3's C5c bounds `|x_front - x_anchor|` by
`(deadBand + 1) * dx = 0.08 mm`, derived from the control law. The sawtooth's measured amplitude is
exactly the dead band, 3 cells = `0.06 mm`, and all five points here fall inside `0.08 mm` (worst
`|3.440195 - 3.5| = 0.0598 mm`). The derivation and the mechanism agree.

## 4. What this measurement does NOT establish

```text
NOT a dedicated ladder   The five points come from runs made for TD-027's C4 and TD-029's leg A.
                         Same tree, same fixture, so they are comparable -- but the budgets are
                         whatever those criteria happened to need (15283, 27511, 33624, 39738,
                         45851), so the sampling is coarse and uneven. ONE reset is observed, and
                         one reset does not establish a period.
NOT established          the drift rate's law. §2's three estimates decrease 1.27 -> 1.11e-04, and
                         four points cannot separate slow convergence from linear drift. A
                         dedicated budget ladder with fine spacing across one reset would settle it.
NOT established          that the sawtooth amplitude is always the dead band. It is 3 cells here
                         because the band is 3.0; TD-040's dead-band ladder varied that setting but
                         did not track the front's trajectory within a march.
NOT established          what sets the drift rate. A front drifting upstream at fixed mass flux
                         means the imposed mdot does not quite balance the flame's own propagation
                         -- which is the eigenvalue question TD-040 owns, and this does not resolve
                         which value is right.
NOT measured             whether a LONGER march eventually settles. The longest point here is
                         45 851 steps; TD-031's own record reached 51 583. Neither is steady.
NOT re-measured          TD-028 and TD-030, the remaining items on the alternating field.
```

**§4's first item is the honest limit of this document.** It assembles a real finding from evidence
gathered for other purposes, at zero additional cost, and the finding is sound — but a dedicated
ladder across one reset is what would turn "a sawtooth with one observed reset" into a measured
period and rate. That run is not performed here.
