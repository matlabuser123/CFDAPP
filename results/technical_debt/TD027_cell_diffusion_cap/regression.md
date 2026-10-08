# TD-027 — REGRESSION — C5 PASS, C6 PASS, and five named failures

> **ATTRIBUTION CORRECTION, 2026-10-06.** TD-027's `GATE.md` §4.4/§5.1 and `audit.md` §2/§3 state
> that the cap GOVERNS TD-028's limit cycle, and that TD-028 would resolve as a consequence. TD-031
> `logs/01` refutes both: the cycle persists at `D = 5` on a 250-cell mesh. **Every criterion and
> verdict in this file stands** — the correction is to the mechanism, not the measurements — but
> TD-028 is **suppressed at one configuration, not resolved.** See
> `attribution_correction.md`.

```text
gate     GATE.md sha256 56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75
change   maximumCellDiffusionNumber 20.0 -> 5.0
runs     logs/03 (A) · logs/04 (B) · logs/05 (C) · logs/06 (D), staged by predicted cost
binary   CFDSpeciesTests sha256 489180ece9c683b8b8e2546d295daa28a87c1757d1f9e09eb44f59137d1656c8
         rebuilt whole-project, fail-closed, and proved newer than the newest source
```

---

## 1. C5 — the counts outside the flame: **PASS**

```text
CFDSpeciesTests (excluding G8PremixedFlameTest)   343   expected 343   OK
CFDIoTests                                        331   expected 331   OK
CFDAppLayerTests                                   32   expected  32   OK
```

Zero failed lines in all three. The default is reachable only through `PremixedFlameSettings` and
nothing outside the flame path constructs one, which this confirms rather than assumes.

## 2. C6 — the per-test cap: **PASS**

```text
case                                                 verdict    before (cap 20)   now
MalformedConfigurationsAreRefused                    PASS    0s        0s         0s
TheBurntCompositionConsumesTheFuelExactly...         PASS    0s        0s         0s
TheObviousChecksAreVacuousOnTheSeededProfile         PASS    0s        0s         0s
AVariableConductivityChangesTheAnswer...             FAIL    1s     FAIL 0s       unchanged
ItConvergesAndReachesTheIndependentlyDerived...      FAIL  812s     PASS 256s     <- MOVED
TheConsumptionIntegralAgreesWithTheEigenvalue        FAIL  850s     FAIL 256s     clause changed
TheFlameSpeedConvergesUnderMeshRefinement            FAIL  968s     PASS 301s     <- MOVED
TheEigenvalueIsIndependentOfTheInitialMassFlux...    PASS 1293s     PASS 471s
TheAnswerIsInsensitiveToTheStepsPerTrial             PASS 1321s     PASS 759s     now for the
                                                                                  RIGHT reason
TenTimesThePreExponentialMovesTheFlameSpeedFar       PASS 1791s     PASS 798s
TheEigenvaluePlateausInTheAnchorPositionAnd...       FAIL 1734s     FAIL 910s     2 anchors now
WithReactionsDisabledNoFlamePropagates               PASS   51s   UNMEASURED      <- see §5
---------------------------------------------------------------------------------------
6 PASS · 5 FAIL · 0 TIMEOUT        longest 1791 s against the 7200 s cap
suite total 8821 s = 2.45 h        predicted 4.1 h
```

**No case breached the cap, so C6 passes.** The suite came in at 2.45 h against my predicted 4.1 h
for a reason that matters: **the tests stop at `finalMarchRelaxationTimes = 10`**, which §4 shows is
inside the startup transient at this resolution. They are cheaper than predicted because they stop
early, not because the fix is cheaper than measured.

## 3. THE FAIL-OPEN SWEEP NOW PASSES FOR THE RIGHT REASON

```text
TheAnswerIsInsensitiveToTheStepsPerTrial, at cap 5:
  16 drift window   S_L 4.67194   S_c 4.19794   delta 0.982081   43893 steps
  32 drift window   S_L 4.67194   S_c 4.19794   delta 0.982081   44005 steps
  64 drift window   S_L 4.67194   S_c 4.19794   delta 0.982081   44293 steps
  drift-window spread: 0

at cap 20 the same test passed with spread 6.8e-04 while:
  16 and 64 were in the BAD mode   balance 0.3647   delta 0.7365
  32 was good                      balance 0.0153   delta 1.2289
```

All three configurations now return the **identical state** to six figures, with step counts that
differ — so the knob moved the solve and genuinely does not matter. **TD-005's F1 is resolved:** the
sweep that certified stability while two thirds of its sample was inadmissible now certifies it
truthfully. This is the strongest single confirmation that the limit cycle was the cause.

## 4. FIVE FAILURES: ONE CAUSE EXPLAINS THEIR MAGNITUDES, TWO OTHERS EXPLAIN THEIR FAILURES

> **Framing corrected 2026-10-06.** An earlier version of this section was headed *"four of them
> share one remaining cause"*, and I reported to the user that both `PASS -> FAIL` transitions were
> explained by the march-length default. Checking against the CONVERGED values shows otherwise:
>
> ```text
> ItConverges...          converged balance 0.0847 against < 0.02  -> STILL FAILS, 4.2x.  TD-030.
> TheFlameSpeedConverges  refinement differences GROW               -> TD-029, not march length.
> TheConsumptionIntegral  converged 0.0847 against < 0.05           -> STILL FAILS, 1.7x.  TD-030.
> ```
>
> The march length (now **TD-031**) explains the *magnitudes* these tests report and fixes
> `ThePlateau...` outright, but it does **not** turn either `PASS -> FAIL` transition back. §6.1's
> gap therefore stands on TD-029 and TD-030, not on TD-031.

### 4.1 `finalMarchRelaxationTimes = 10` is a second unvalidated resolution default

TD-005 `logs/19` measured the front offset against march length at **this exact resolution**:

```text
relaxT 10   offset  -34.25 cells    <- the default EVERY test uses
relaxT 14   offset -124.70 cells
relaxT 18   offset   +1.46 cells    at the anchor
relaxT 22   offset   +2.01 cells    at the anchor
relaxT 26   offset   +2.56 cells    at the anchor
relaxT 30   offset   +0.11 cells    at the anchor
```

The march **does** converge, by relaxT 18 — which is why TD-027's C4 family starts there. Every test
stops at 10, inside the transient. That accounts for:

| failure | measured | the transient explains it |
| --- | --- | --- |
| `ItConverges…` | `fuelBalanceResidual 0.093590` against `< 0.02` | the converged value is `0.0847` (C4); `0.0936` is the transient |
| `TheConsumptionIntegral…` | `0.093590` against `< 0.05` | same number, same cause |
| `ThePlateau…` anchor 0.30 | front off by `32.4` cells | `logs/19` measured `-34.25` at relaxT 10 |
| `ThePlateau…` anchor 0.40 | front off by `35.6` cells | same |
| `TheEigenvalueIsIndependent…` | passes, but `S_c` differs 20 % and `delta` 30 % between guesses | both states are transients, so they retain path memory |

**It was invisible at cap 20** because the state never converged there at all — "converged at
relaxT 10" was a phase of the limit cycle. Suppressing the cycle exposed it. **This is a new
register item**, and it is the obvious next fix.

### 4.2 The one failure that is NOT the march length, and is not over-claimed

```text
TheFlameSpeedConvergesUnderMeshRefinement   FAIL
  125 cells  S_L 4.82950   S_c 1.98162   delta 0.528732
  250 cells  S_L 4.82296   S_c 3.28762   delta 0.790080
  500 cells  S_L 4.67194   S_c 4.19794   delta 0.982081
  refinement differences 0.00653 then 0.151 cm/s -- they GROW 23x, so `fine < coarse` fails
```

**This is TD-029 confirmed by its disappearance.** The test passed at cap 20 only because the
temporal error cancelled the spatial one; with the temporal error 48x smaller, the spatial
convergence is exposed and it is **not converging**. Each level is also a transient (§4.1), so the
comparison may be between transients — plausible, **not tested**, and not folded into §4.1's count.

**A genuinely new lead:** `S_c` rises `1.98 -> 3.29 -> 4.20` against `S_L` of `4.83 -> 4.82 -> 4.67`,
i.e. 41 % -> 68 % -> 90 % of the eigenvalue as `dx` falls. **TD-030's reaction-integral excess looks
spatial** — an under-resolved reaction zone — which is sharper than the operator-split candidate
recorded in TD-005 §P.4.

### 4.3 `AVariableConductivity…` is unchanged and already named

```text
test_premixed_flame.cpp:624
  Expected: risingConductivity.value(kExpectedAdiabatic) > 3.0 * kConductivity
  actual:   0.11 vs 0.15
```

Fixture arithmetic, independent of `dt`. FAIL before, FAIL now, named in TD-005 `GATE.md` §5.2.

## 5. STAGE D: the no-flame control PASSES in 51 s — and I CANNOT attribute it

```text
WithReactionsDisabledNoFlamePropagates    PASS    51 s
  reactions off: status 1 (MaxIterations), S_L = 0.0144343 cm/s, S_c = -0, peak |S_E| = 0,
  brackets = 13
```

No heat release, no flame, test passes. **But this case was never measured at the repository's cap
on the unchanged tree** — TD-004's baseline recorded it as TIMEOUT at a **900 s harness** cap, and
TD-005's `GATE.md` §5.2 explicitly listed it as *"STILL NOT MEASURED AT THE REPOSITORY'S CAP"*. I
wrote `run_noflame_control_at_repo_cap.sh` for TD-005 and never ran it.

**So I cannot say TD-027 fixed this.** It may well have passed at cap 20 under a 7200 s cap too. The
control I need is the same two-library comparison TD-005 used for C4d, and it has not been run.

### 5.1 THE CONTROL IS IN (`logs/08`): it passes at cap 20 too, in 42 s

```text
reconstruction   sha256 c962020a2254e721037840a70a0d2ab30f48c6e4f24ae131313b71e266743fdf
                 MATCHES the frozen pre-fix hash exactly
built header     Real maximumCellDiffusionNumber{20.0};      cross-checked in the log
result           PASS, 41 858 ms, status 1, S_L 0.0144343 cm/s, peak |S_E| = 0, brackets = 13
                 against PASS 51 s at cap 5
```

**TD-027 gets NO credit for this.** And my guess was wrong in the other direction too: I expected a
cap-20 pass, but assumed the 51 s was at worst *unattributable*. It is attributable, to something
else.

**What actually fixed it.** TD-020's entry describes the cost precisely: *"early trials still locate
a front and return a sided verdict; the bracket then grinds through `maxOuterIterations = 2000` at
up to 20 000 steps each"*. The run now reports **`brackets = 13`** and exits -- the grind does not
happen. That is the signature of **TD-004's locator fix**: a locator that refuses implausible
crossings loses the washed-out front sooner, `!sided` fires, and `NoFlame` short-circuits.

```text
TD-004 logs/06, PRE-locator-fix      TIMEOUT at 900 s   -- plausibly a real cost at that cap
now, POST-locator + C4d, cap 20      PASS 42 s
```

**NOT ESTABLISHED:** which of TD-004's locator fix or TD-005's C4d did it. I have not isolated them,
and this control only excludes TD-027. The locator fix is the candidate because the exit path
matches TD-020's described mechanism, not because it has been separated.

### 5.2 What this unblocks, which is worth more than TD-027's own verdict

```text
G8 negative control 1   AVAILABLE. TD-020 recorded it as a TD-006 blocker on the strength of a
                        900 s timeout that is no longer real. G8 can be exercised with BOTH of
                        its negative controls.
TD-005 C5               its third control is NOT unexercisable. C5 can be completed.
TD-020                  its stated premise -- "the no-flame case costs MORE than the flame case"
                        -- is FALSE: 42 s against the flame cases' 256-1791 s. The item needs
                        re-characterising and may be closeable.
```

### 5.3 A method note: the hash proof earned its keep

The first attempt at this control was **REFUSED**
(`logs/07_stage_d_control_REFUSED_bad_reconstruction.log`). My reverse-apply forgot that the forward
patch also inserted a blank line, so the reconstruction came out `49080be4...` against the frozen
`c962020a...`. The proof caught it, declined to build a control on a tree that was not the frozen
one, and the trap restored the post-fix header. **Without that proof the control would have run on a
one-blank-line-different tree and its verdict would have been reported as a cap-20 measurement.**

## 6. Verdict against the gate

```text
C1  the cap IS the realized D          PASS   D = 4.999999999999999, rel 1.78e-16 vs 1e-12
C2  S_L within the temporal budget     PASS   0.084 % vs 0.28 %
C3  reported precision coherent        PASS   1.7x vs 2x
C4  the limit cycle suppressed         PASS   max-min 8.07e-04 <= mean 8.48e-02, by 105x
    *** INVALID AS AUTHORITATIVE 2026-10-06: all four budgets ODD, see TD-032 ***
C5  nothing changes outside the flame  PASS   343 / 331 / 32
C6  no case breaches 7200 s            PASS   longest 1791 s, 0 timeouts
```

**All six criteria PASS.**

### 6.1 A GAP IN MY OWN GATE, recorded rather than passed over

The gate has **no criterion for the flame tests' own verdicts**. Requiring them unchanged would have
been wrong — several were passing for the wrong reasons, and the point of the fix is to stop that —
but TD-005's gate handled exactly this situation by requiring `G8PremixedFlameTest` *"reported test
by test, each one's movement accounted for individually"*, and I did not carry that clause over.

**Consequence:** the two PASS -> FAIL transitions in §4 are **new failures the frozen gate does not
name**, and CLAUDE.md §8 permits only the failures a gate names. They are reported as new failures
and are **not** reclassified by the explanations in §4, however well those explanations fit. Whether
TD-027 may be closed with two unnamed new failures standing is not a question this gate can answer
about itself.

---

# TD-032 NOTE — C4 IS INVALID AS AUTHORITATIVE (recorded 2026-10-06, after this item closed)

Nothing above is deleted or altered beyond the two inline markers that point here.

**What was found.** `TD031_final_march_length/logs/05_cell35_history.log` stopped the final march at
five consecutive budgets and found the reported field **alternating every single step** between the
cold preheat zone (~310 K) and a nearly fully burnt field (~1790 K) across cells 25–45. The root
cause is `src/species/PremixedFlame.cpp:1189-1191`: the transient term's old time level is copied
**before** the state is updated, so it trails the state by one step from step 2 onward, the
transient path connects step `k-1` to step `k+1`, and the even and odd steps become two interleaved
sub-sequences that need not share a fixed point. Audit:
`technical_debt/TD032_lagged_time_level/audit.md`.

**Why C4 specifically cannot stand.** C4's four levels — relaxT 18/22/26/30 — have march budgets
30949, 37827, 44705 and 51583. **All four are odd**, because consecutive levels differ by 6878, an
even number, so the ladder preserves step parity *by construction*. `max-min 8.07e-04 <= mean
8.48e-02, by 105x` is therefore four samples of one branch of the alternation. It measures
reproducibility of a parity, not convergence of a march.

**What is unaffected.**

```text
C1  D = cap, bitwise to 1.78e-16          reads dt and alpha_max, never the march's field
C2  S_L converges in dt, Richardson        taken from the SIGN of the trial drift; see the
    limit 4.67589 cm/s, gated value          narrowing in TD-032 audit section 5 -- the trials
    +3.99 % off                              run the same advance(), so this is evidence the
                                             SIGN is robust, NOT that the march is correct
C3  derived from C2                        same standing as C2
C5  343 / 331 / 32                         outside the flame entirely
C6  no case breaches 7200 s                wall times, not fields
C7  verdict-movement accounting (A1)       counts verdicts, not field values
```

**The shipped change is not withdrawn.** `maximumCellDiffusionNumber 20.0 -> 5.0` rests on C1, C2
and C3, which are the criteria that chose the value 5.0. What is withdrawn is the claim "the limit
cycle is suppressed", and with it the ground on which this item was marked RESOLVED.

**Whether to reopen TD-027 is a user decision.** It is recorded here, in `TECHNICAL_DEBT.md`, in
`TODO.md` and in `TD031_final_march_length/PARITY_FINDING.md` §6 — and deliberately not acted on.
