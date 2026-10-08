# TD-004 VERIFICATION A2 — the locator fix WORKS, and the item STOPS on what it exposed

```text
STATUS:            TD-004 OPEN / STOPPED. The locator fix is implemented, verified and RETAINED.
                   Stopped on a PRE-REGISTERED condition: S_L moved +6.07 %.
                   New blocker carved out as TD-040.
SCOPE:             `recentre()` translating the locator's tracked position. Nothing else.
GATE:              GATE_A1.md  e57bc758ca5610eb…  frozen, unchanged, never re-opened.
                   C2-A1's three clauses are SATISFIED by the measured data (§2).
RESULT:            mechanism repaired · C2-A1 (a)(b)(c) satisfied · S_L +6.07 %  -> STOP

FILES CHANGED:     src/species/PremixedFlame.cpp  8c85bc9f… -> 3c2263cd…   (1 statement + comment)
TEST FILES:        none.  HEADER: none.
EVIDENCE:          A2_01 counters · A2_02 rejections · A2_03 trace · A2_04 verification ·
                   A2_05 ledger (two libraries)

REGRESSION:        NOT RUN. The item stopped before it; §8 regression is not spent on a change
                   that is not being closed.
COMMIT:            none — not authorized, and TD-034 blocks commits from this tree.
```

---

## 1. The mechanism is repaired

```text
budget    front mm  offCells  finalRc  rej<10  rej10-50  rej>50
     1    3.500014     0.001        0       0         0       0
   500    3.556045     2.802        1       0         0       0
  5000    3.524840     1.242        3       0         0       0
 15283    3.499220    -0.039        3       0         0       0
```

The front ends **0.039 cells from the anchor** against −51.267 before. `finalRc` grows past 1, so the
locator keeps tracking through each recentring. **Every rejection counter is zero across the whole
solve** — including the trials, which previously carried 12, so the fix repaired their locator too.

## 2. C2-A1's three clauses are satisfied by the data

```text
relaxT      10        14        18        22        26        30
balance  6.303e-03 6.220e-03 6.139e-03 6.062e-03 5.986e-03 6.235e-03    all admissible
delta mm   1.1964    1.1963    1.1965    1.1967    1.1963    1.1964

(a) REACHES  relaxT 10 at 6.303e-03 < 0.05                    satisfied
(b) STAYS    all six levels admissible                        satisfied
(c) SETTLES  max-min 3.170e-04 <= mean 6.157e-03              satisfied
```

The balance improved **fifteenfold**, from ~9e-02 to ~6e-03, and `delta` is constant to four figures.

**The probe declined to issue this verdict**, stopping on its hardcoded `GGBGGB` expectation
(TD-039). The determinism its guard is concerned with was separately established by `A1_03`/`A1_04`
being byte-identical. The clause arithmetic above is read from the probe's own printed table.

**This is NOT recorded as C2-A1 PASS.** The criterion's clauses are satisfied, and the item stops on
§3 — a condition outside C2-A1 that was registered before the run. Reporting a PASS while a
registered stop has fired would be exactly the selective reading this campaign exists to prevent.

## 3. THE STOP — `S_L` moved, as the audit said it must not

`audit_A2.md` §6 registered: *"if S_L moves, the fix changed the eigenvalue, which it must not: the
locator feeds recentring, and recentring is supposed to be conservation-neutral. That would be a
stop."*

```text
S_L   4.671937 -> 4.955332 cm/s     +6.07 %
TD-027 C2's Richardson limit 4.67589 cm/s:   pre-fix -0.08 %,  post-fix +5.98 %
```

The pre-fix eigenvalue matched an independently extrapolated limit to 0.08 %; the post-fix one is
6 % away from it.

## 4. The ledger says WHY, and it is a second defect — `A2_05`, two libraries

```text
                           PRE-FIX          POST-FIX
recentrings, total         121              223
   in the final march      1                3
fuel ledger / fuelThrough  -0.793 %         -2.523 %
per recentring, fuel       -3.533634e-06    -3.533419e-06  kg/m^2
per recentring, energy      2.120180e+01     2.120052e+01  J/m^2
```

**The per-translation cost is IDENTICAL between the two libraries — agreeing to 6e-05 relative.**
That one fact carries the argument:

```text
it KILLS the instrument-defect reading   ledger/per-recentring is exactly 3.0000 post-fix and
                                         1.0000 pre-fix; the ledger measures what it claims
it shows WHY the ledger grew             the COUNT tripled, not the cost per event
it shows translation is NOT free         -3.53e-06 kg/m^2 and +21.2 J/m^2 per event,
                                         i.e. -0.79 % of fuelThrough each
```

**And the eigenvalue mechanism is in the TRIALS, not the final march.** `S_L` comes from bisection on
drift signs across the trials, and the trials went from **120 recentrings to 220**. The final march's
ledger is reset before it begins, so the 3-vs-1 comparison *understates* the total: the trials now
perform nearly twice as many non-conservative translations, each removing fuel, and a flame losing
more fuel needs more mass flux to sustain. Same order as the shift, and in the right direction.

**So the locator fix is correct and insufficient.** It does exactly what it was designed to do and
exposes a residual non-conservation in `recentre()` that was previously hidden by the operator
almost never firing. TD-004's register entry records recentring non-conservation as *"fixed"*; the
ledger shows it was **reduced, not eliminated**, and the ledger exists precisely to measure that
residue.

Same pattern as TD-032 -> TD-037: a correct fix makes a latent downstream defect reachable.

## 5. Carved out

```text
TD-040  `recentre()` is NOT conservation-neutral: -3.53e-06 kg/m^2 of fuel and +21.2 J/m^2 of
        sensible energy per translation, which is -0.79 % of fuelThrough per event. Previously
        masked because the blind locator let the operator fire once per march; with the locator
        fixed it fires 3x in the march and 220x across the trials, and the eigenvalue moves
        +6.07 %. Any fix that makes recentring fire MORE will move S_L until this is addressed.
```

## 6. What is NOT established

```text
NOT established  that the ledger is the WHOLE of the 6.07 %. The arithmetic from "fuel removed per
                 translation" to "eigenvalue shift" is not closed, only shown same-order and
                 same-sign. Closing it needs the dt-ladder re-measured on the fixed tree, giving a
                 Richardson limit not inherited from the blind-locator solver.
NOT established  which eigenvalue is CORRECT. The pre-fix value agreed with TD-027's limit, but
                 that limit was extrapolated on the blind solver and may have inherited the defect.
                 4.955332 is not therefore "wrong" -- it is unverified, as is 4.671937.
NOT closed       C2-A1, and so not TD-004. Its clauses are satisfied; the item stops on §3.
NOT run          any regression. The fix is retained but the item is not being closed.
NOT re-measured  TD-027's C4, TD-029, TD-031, which remain on the pre-TD-032 field.
```

## 7. The fix is RETAINED

It is correct about the locator, the frozen gate's clauses are satisfied, and reverting it would
restore a solver that refuses the true front once per step for 16 000 steps. The eigenvalue question
belongs to TD-040, which is where a translation operator's conservation properties should have been
all along.
