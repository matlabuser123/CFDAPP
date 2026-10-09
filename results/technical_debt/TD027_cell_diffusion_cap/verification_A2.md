# TD-027 A2 — C4 RE-MEASURED on a parity-crossing ladder: PASS. But C2's reference value is now stale.

```text
STATUS:      C4 RE-MEASURED AND PASSES, on a ladder measured to cross parity. The ground on which
             C4 was marked INVALID AS AUTHORITATIVE is removed.
             TD-027 is NOT thereby closed -- see §4. C2's Richardson limit was extrapolated on the
             defective solver and now disagrees with the measured eigenvalue by +5.98 %.
SCOPE:       C4 only. No production change. The shipped `maximumCellDiffusionNumber 20.0 -> 5.0` is
             unchanged and was verified still in force (`PremixedFlame.hpp:267` reads `5.0`).
CRITERION:   GATE.md C4 — "the limit cycle suppressed", `max-min <= mean` over the relaxT family.
RESULT:      ladder parity-crossing (2 EVEN / 2 ODD) · max-min/mean = 0.0408 · PASS by 24.5x
EVIDENCE:    logs/A2_01_c4_parity_remeasure.log
INSTRUMENT:  tools/c4_parity_probe.cpp
TREE:        PremixedFlame.cpp fa815c97… · libcfdcore.a 852555cf… (the A3-verified pair)
REGRESSION:  NOT RUN. No production change, and TD-035 blocks the §8 ladder.
COMMIT:      none — not authorized.
```

---

## 1. Why C4 needed re-measuring

`regression.md` §TD-032 recorded C4 as **INVALID AS AUTHORITATIVE**: its four levels, relaxT
18/22/26/30, had budgets `30949, 37827, 44705, 51583`, **all odd**, because consecutive levels differ
by `6878` — an even number. The ladder preserved step parity *by construction*.

With TD-032's stale time level splitting the march into two interleaved sub-sequences, the recorded
`max-min 8.07e-04 <= mean 8.48e-02` was **four samples of one branch of the alternation**. It
measured the reproducibility of a parity, not the convergence of a march.

## 2. The re-measure, on the TD-032-fixed march

```text
relaxT       budget   parity   fuel balance     S_L cm/s   front mm   wall s
    18        27511      ODD   6.139263e-03     4.955332   3.468162     1060
    22        33624     EVEN   6.061506e-03     4.955332   3.453799     1120
    26        39738     EVEN   5.986308e-03     4.955332   3.440195     1196
    30        45851      ODD   6.235249e-03     4.955332   3.485637     1276

EVEN budgets 2    ODD budgets 2    ->  PARITY-CROSSING
```

**The parity is measured and printed, not assumed.** That is the whole methodological point: the
original ladder's defect was invisible because nobody printed the parity. The probe was written to
**refuse a C4 verdict** if the family turned out parity-preserving again; it did not have to.

```text
max       6.235249e-03
min       5.986308e-03
max-min   2.489408e-04
mean      6.105582e-03
ratio     0.0408        criterion: <= 1

C4   max-min <= mean    PASS, by 24.5x
```

## 3. The result that was not asked for: `S_L` is identical to six decimals

```text
relaxT 18   S_L 4.955332
relaxT 22   S_L 4.955332
relaxT 26   S_L 4.955332
relaxT 30   S_L 4.955332
```

**Four budgets spanning 27 511 to 45 851 steps — a 1.67x range — and BOTH parities, and the
eigenvalue does not move in the sixth decimal.** C4 only required the balance spread to be bounded
by its mean; this is far stronger, and it is the most direct evidence yet that TD-032's alternation
is genuinely gone rather than merely averaged over.

It also corroborates TD-032's own closure from an instrument TD-032 did not use: TD-032 verified
window-independence, while this varies march LENGTH across parity and gets the same eigenvalue.

The front position does vary — `3.440` to `3.486 mm` — and every value sits inside C5c's derived
`0.08 mm` dead-band bound (worst `|3.440195 - 3.5| = 0.0598 mm`). So the drift is the recentring
controller working as specified, not residual alternation.

## 4. WHY THIS DOES NOT CLOSE TD-027

C4's invalidation was the stated ground for reopening the item, and that ground is now removed. But
closure needs the whole gate, and **C2 has a problem C4's pass does not touch**:

```text
C2   "S_L converges in dt, Richardson limit 4.67589 cm/s, gated value +3.99 % off"
     The limit 4.67589 was extrapolated on the BLIND-LOCATOR solver.
     Measured now:  4.955332 cm/s  ->  +5.98 % from that limit.
```

Pre-fix, `S_L = 4.671937` agreed with the `4.67589` limit to **0.08 %** — which is exactly why the
limit looked trustworthy. Post-fix the eigenvalue is 6 % away from it. Either:

```text
the limit inherited the locator defect, and 4.67589 is not the true dt -> 0 limit; or
the post-fix eigenvalue is wrong, and TD-004's locator fix introduced an error.
```

**Neither is established.** This is TD-040's open question — *"which eigenvalue is correct? Neither
4.671937 nor 4.955332 is verified"* — and `TD040/audit.md` §5b already names the measurement that
would settle it: **re-run the dt ladder on the fixed tree**, giving a Richardson limit that has not
inherited the defect.

So TD-027's status after this run is: **C4 resolved, C2's reference value stale, item still open.**
Marking it RESOLVED on C4 alone would repeat the error that reopened it — closing on a criterion
while an unexamined one carried a defect.

## 5. What is NOT established

```text
NOT established  which eigenvalue is correct. §4. This run makes 4.955332 look very robust -- four
                 march lengths, both parities, six identical decimals -- but ROBUST IS NOT CORRECT.
                 A systematically biased eigenvalue can be perfectly reproducible, and TD-040's
                 recentring ledger is a candidate bias that this probe does not test.
NOT re-measured  C2 and C3. They were recorded as "unaffected" by the TD-032 note on the grounds
                 that they read the trial drift's SIGN rather than the march's field -- but their
                 REFERENCE VALUE (the Richardson limit) is pre-fix, which the note did not consider.
                 That is a gap in the note, found here.
NOT re-measured  C1, C5, C6, C7. The TD-032 note's reasoning for those stands: they read dt and
                 alpha_max, test counts, or wall times, never the march's field.
NOT established  that the balance's residual ~6.1e-03 is correct in absolute terms. C4 bounds its
                 SPREAD, not its value, and 6.1e-03 against the 2 % gate is a near-miss that
                 audit.md §5 already flagged as "must be resolved, not rounded".
NOT run          the §8 regression.
```

**§5's second item is the finding to carry forward.** The TD-032 note classified C2 as unaffected
because of what it reads during the march. That was right about the mechanism and incomplete about
the reference: a criterion can be structurally immune to a defect and still be gated against a
number that was contaminated by it.
