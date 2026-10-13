# THE MARCH ALTERNATES BETWEEN TWO FIELDS EVERY STEP — and parity predicts all 30 verdicts

```text
recorded    2026-10-06
evidence    TD031 logs/05 (the cell-35 history), re-read across logs/01, logs/02 and
            TD005 logs/14, logs/15
severity    invalidates TD-028's entire characterisation, TD-031's convergence point, and
            TD-027's C4 verdict -- which TD-027 was CLOSED on.
```

---

## 1. The measurement

Five consecutive march steps at the configuration three separate measurements called converged
(500 cells, cap 5, relaxT 18), with the march stopped at successive budgets:

```text
budget   T[25]      T[30]      T[35]      T[40]      T[45]
 30945    308.09     310.39     312.99     315.95     319.30     COLD
 30946   1728.12    1784.00    1796.61    1799.29    1799.85     ALL HOT
 30947    308.09     310.39     312.99     315.95     319.30     COLD
 30948   1728.03    1783.98    1796.61    1799.29    1799.85     ALL HOT
 30949    308.09     310.39     312.99     315.94     319.29     COLD
```

**The entire region upstream of the front alternates, every single step, between the cold preheat
zone (~310 K) and a nearly fully burnt field (~1790 K).** No flame consumes and un-consumes its
preheat zone every 9.4 microseconds. This is a numerical defect, not physics.

It is also not an instrument artefact: the cold branch drifts slowly and monotonically
(`308.0932 -> 308.0917 -> 308.0901`), which is the physical state evolving, so one branch is real and
the other is not.

> **CORRECTED 2026-10-06, see section 8.** The last clause — "one branch is real and the other is
> not" — is wrong. The root cause is a one-step-stale transient time level, which splits the march
> into two interleaved sub-sequences; *both* branches are sub-sequences of the defective scheme and
> neither solves the intended step. The monotone drift shows each branch has its own transient, not
> that one of them is the physical state. The measurement above stands; that inference does not.

## 2. PARITY PREDICTS EVERY VERDICT I HAVE TAKEN

The march budget's parity determines which branch is reported. Applied to every good/bad
classification in this investigation:

```text
configuration        points   parity predicts the verdict
500 / cap 20  logs/14   10    YES     even -> good, odd -> bad
500 / cap 20  logs/15    5    YES     odd -> bad
500 / cap  5  logs/02    5    YES     even -> bad, odd -> good
500 / cap 10  logs/01    5    YES     even -> bad, odd -> good
250 / cap  5  logs/01    5    YES     even -> good, odd -> bad
---------------------------------------------------------------
ALL 30 MEASUREMENTS predicted, in every configuration
```

The polarity differs between configurations — which branch is "good" depends on where the
alternation starts relative to the budget — but **within each configuration the correlation is
perfect across every point measured.**

## 3. What this invalidates

```text
TD-028's CHARACTERISATION, entirely
   "a nearly square limit cycle of period 14 relaxation times, amplitude 165 cells, undiminished
   at 8 periods, duty cycle 29-57 %" -- there is no such cycle. The period-14 model fitted the
   PARITY of 397-ish steps per relaxation time, where integer rounding makes the parity of
   `relaxT * steps-per-relaxT` effectively arbitrary along a ladder. Seventeen verdicts
   "explained by one parameter" were seventeen parities.
   logs/15's "amplitude undiminished at 8 periods": all five budgets were ODD.

TD-031's CONVERGENCE POINT
   "converges at relaxT 18" -- budgets 17194(e), 24072(e), 30949(o), 37827(o), 44705(o) against
   verdicts bad, bad, good, good, good. The relaxT-18 "transition" is the even/odd flip.

TD-027's C4 VERDICT -- and TD-027 WAS CLOSED ON IT
   C4 measured relaxT 18/22/26/30, budgets 30949, 37827, 44705, 51583. ALL FOUR ODD. The step
   between consecutive levels is 6878, an EVEN number, so the ladder preserves parity by
   construction. C4's "max-min 8.07e-04 <= mean 8.48e-02, by 105x" is four samples of ONE BRANCH,
   not convergence.

THE ATTRIBUTIONS, both of them
   "the cap governs the cycle" was refuted and replaced with "absolute dt governs the cycle".
   Both were about a phenomenon that is not what either described.
```

## 4. What SURVIVES, and why

```text
TD-027 C1   D = cap, bitwise to 1.78e-16. Reads dt and alpha_max, not the field.         SURVIVES
TD-027 C2   S_L converges in dt; the +3.99 % error; Richardson limit 4.67589 cm/s.
            S_L is fixed by the TRIALS' drift signs before the final march begins, which is
            why it has been identical across 30+ solves at every march length and every
            parity. The dt ladder also used relaxT 1.0, a short march.                  SURVIVES
TD-027 C3   derived from C2.                                                             SURVIVES
TD-027 C5   343 / 331 / 32, outside the flame entirely.                                  SURVIVES
TD-027 C6   no case breaches 7200 s -- wall times, not fields.                           SURVIVES
TD-005 C4d  flamePosition was an echoed input and is now measured. Independent of this.   SURVIVES
TD-002,     everything outside the premixed-flame march.                                 SURVIVES
TD-001
```

**The eigenvalue and the cap validation stand. Everything derived from the reported FIELD does
not.** That split is sharp because `S_L` comes from the trials and the field comes from the march.

## 5. Two phenomena, not yet unified — stated rather than merged

`logs/02`'s field dump of the "bad mode" (window 16, cap 20) showed a **displaced but otherwise
normal flame** — `T` rising 322 K to 1800 K with the front at 2.57 mm — not an all-hot field. The
alternation measured here produces an all-hot field. So there are plausibly **two** distinct
things: the step-parity alternation, and a displaced front.

**I am not merging them.** Four readings in this item have already been wrong by unifying things
that measured differently.

## 6. What this means for the closed item

TD-027 was closed with C1–C7 all passing. **C4's verdict rests on same-parity sampling and cannot
stand as measured.** The fix it shipped — `maximumCellDiffusionNumber 20.0 -> 5.0` — is still
justified by C2 and C3, which survive and which were the reasons the cap value was chosen. But
"C4: the limit cycle is suppressed" is not a result, and the item's closure should be revisited on
that basis. That is the user's call, not mine, and it is recorded here rather than acted on.

## 7. The defect to fix, and what it is upstream of

```text
THE MARCH'S REPORTED FIELD ALTERNATES, EVERY STEP, BETWEEN THE PHYSICAL STATE AND A
NEARLY FULLY BURNT ONE.
```

Candidates not yet separated: a double-buffer or state-swap error in `advance()`; an operator-split
intermediate (a chemistry half-step evaluated at the burnt limit) being left in the reported array;
or the implicit source linearisation. **No guess is offered** — the next step is to read `advance()`
and the split directly, which is the first thing in this chain that is not downstream of the
alternation.

It is upstream of TD-004's C2, TD-005's C3 and C6, TD-028, TD-030 and TD-031. **Every field-derived
quantity in the premixed-flame evidence base was measured on one of two alternating fields**, and
which one depended on the parity of a step count nobody was choosing deliberately.

---

## 8. ROOT CAUSE FOUND — added 2026-10-06, after this document was first written

**`src/species/PremixedFlame.cpp:1189-1191` assigns the transient term's OLD TIME LEVEL BEFORE
the state is updated**, so from step 2 onward the transient term trails the state by exactly one
step. The transient path then connects step `k-1` to step `k+1`, the even and odd steps become
two sub-sequences coupled only through the operator coefficients, and two sub-sequences with no
transient coupling need not share a fixed point. **The period-2 alternation is that pair.**

Full audit, including why `densityOld` in the same three-line block is correct, why the existing
transient tests cannot see it, and what a fix would require:
`results/technical_debt/TD032_lagged_time_level/audit.md`.

### Two corrections to this document, recorded rather than rewritten

**Section 1 said "one branch is real and the other is not."** That was wrong. Both branches are
sub-sequences of the same defective scheme, and **neither** is the solution of the
backward-Euler step the code intends. The monotone drift of the cold branch is not a mark of
authenticity; it is what a sub-sequence with its own transient does.

**Section 4's survival argument for C2 was too broad.** "S_L comes from the trials, not the
final march" shields it from the final march's *parity* only. `runTrial` calls the same
`advance()` (line 1374), so the trials carry the same lag. What keeps `S_L` credible is
narrower: it is read from the *sign* of the front drift, it is bitwise reproducible across 30+
solves, and it converges in `dt` to a Richardson limit. That is evidence the sign is robust —
not evidence the march is correct. See TD-032 audit section 5.

### What section 5's second phenomenon now looks like

`logs/02`'s displaced-but-normal flame at 2.57 mm is consistent with being the *other branch* at
a configuration where both sub-sequences still carry a flame, rather than a separate defect. It
is still **not merged**: that is a hypothesis the lag now makes available, and it has not been
tested.

---

## 9. THE REPORTED FRONT POSITION ALTERNATES TOO — added 2026-10-06 from TD-032 logs/01

TD-032's dry-run reported `flamePosition` at five consecutive budgets of the gated fixture:

```text
budget   30945    30946    30947    30948    30949
front     3.535    0.261    3.535    0.261    3.535   mm      (anchor = 3.500 mm)
```

The cold branch puts the front **on the anchor** at 3.535 mm. The hot branch reports **0.261 mm —
at the inlet** — because a nearly fully burnt field puts the `T_anchor` crossing hard against the
upstream boundary.

**That is the shape of TD-004's recorded blocker.** TD-004 stopped at C2 with: *the flame flashes
back and the single `tAnchor` crossing relocates to the inlet independently of upstream room* — and
that reading is what classified the remaining work as flame **stabilisation**, placed it in TD-005's
scope, and inverted the TD-004 -> TD-005 critical path.

**Stated as a lead, not a conclusion, and deliberately not merged.** TD-004's measurements were
taken at `maximumCellDiffusionNumber = 20`, before TD-027 changed the default to 5, so these are not
the same runs and no identity is claimed. What is established is that at the gated fixture the
"front at the inlet" observation is produced by TD-032's alternation and requires no flashback at
all. Section 5's third reading — the displaced-but-normal flame at 2.57 mm in `TD031/logs/02` —
still matches neither 3.535 nor 0.261, so something may remain beyond the alternation.

**Consequence:** TD-004's blocker must be re-measured after TD-032 is fixed, under TD-004's own
gate, before any claim about flame stabilisation or about the critical-path inversion is relied on.
That re-measurement is not in TD-032's scope.
