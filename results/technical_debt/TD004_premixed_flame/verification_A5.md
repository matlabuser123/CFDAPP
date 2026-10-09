# TD-004 A5 — C6 PASSES. C6a FAILS, and its two clauses fail for OPPOSITE reasons.

```text
STATUS:      C6 SATISFIED. C6a FAILED. The gate STOPS AT C6a, so C6b and C7 are NOT evaluated.
             TD-004 remains OPEN / STOPPED, now with a SECOND reason beside TD-040's.
SCOPE:       C6 and C6a of the frozen GATE.md. No production change; the locator fix verified in
             A2 is unchanged and remains TD-004's only production change.
GATE:        GATE.md 12ccc5dd… (C6, C6a unamended) · GATE_A3.md 8a11ad39… (C5, satisfied in A4)
RESULT:      C6 PASS (properties 2, 3, 5) · C6a FAIL (observed order AND fuel balance)
EVIDENCE:    logs/A5_01_c6_c6a.log  ·  baseline comparison from logs/08_fixture_selection.log
INSTRUMENT:  tools/fixture_probe.cpp e9d83b3a…, UNMODIFIED and hash-matched to logs/00_freeze.log
REGRESSION:  NOT RUN. The gate stopped; §8 is not spent on a failing gate, and TD-035 blocks it.
COMMIT:      none — not authorized.
```

---

## 1. C6 — PASS. All three properties hold.

### 1.1 Property 2, independence of the initial guess

```text
mdot_0 = 0.1   S_L 4.95548 cm/s    S_c 4.90444    fuel balance 0.00633378
mdot_0 = 0.9   S_L 4.95316 cm/s    S_c 4.90419    fuel balance 0.00587242
initial-guess spread  0.000469                                    PASS
```

The degeneracy trap audit.md §6 exists to guard stays shut.

### 1.2 Property 3, plateau in anchor position and domain length

```text
anchor 0.30   S_L 4.93859    anchor 0.40   S_L 4.95869    anchor 0.50   S_L 4.95869
anchor plateau  4.93859 .. 4.95869 cm/s   (spread 0.41 %)
domain 40 mm    S_L 4.95869                                       PASS
```

`anchor 0.40` and `0.50` agree to six figures, and the 40 mm domain reproduces them — so the
cold-boundary difficulty is not setting the eigenvalue.

### 1.3 Property 5, convergence under refinement

Judged from C6a's probe output, since property 5 *is* refinement convergence:

```text
cells     63        125       250       500
S_L    4.51340   4.90527   4.94863   4.95533      monotone, differences shrinking
```

### 1.4 S_L against C6's stated baseline, as C6 requires

```text
baseline 4.86230 cm/s  ->  4.95548 cm/s      +1.92 %
```

This is TD-040's shift, now visible through the gate's own instruments rather than a probe. C6 asks
for it to be REPORTED, not bounded, so it is not a failure of C6 — and it remains TD-040's question.

### 1.5 Both tests finished inside the per-test cap

```text
TheEigenvalueIsIndependentOfTheInitialMassFluxGuess          2 472 812 ms  = 2473 s
TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength   3 946 038 ms  = 3946 s
                                                      cap   ctest --timeout 7200
```

## 2. C6a — FAIL. Two clauses, and they fail for opposite reasons.

C6a requires four things of the `{125, 250, 500}` triplet. The frozen probe, rerun unmodified:

```text
cells    S_L cm/s  thickness mm  res cells    steps   status   fuel bal
   63     4.51340      0.804304       5.07     2468       OK     0.5197
  125     4.90527      1.009731      12.62     5422       OK     0.1505
  250     4.94863      1.162471      29.06    19031       OK     0.0283
  500     4.95533      1.196361      59.82    73683       OK     0.0063

triplet            coarse diff      fine diff    order p
{125, 250, 500}  +4.335550e-04  +6.704005e-05      2.693
```

```text
admissible: every level converged                                          HOLDS
admissible: every level > 4 cells of front (12.62, 29.06, 59.82)           HOLDS
admissible: differences shrink (4.34e-04 -> 6.70e-05)                      HOLDS
admissible: no sign change (both positive)                                 HOLDS
observed order p in [1.5, 2.5]                     p = 2.693               FAIL
fuel balance at every level < 0.02        0.1505 / 0.0283 / 0.0063         FAIL at 125 and 250
```

### 2.1 The balance clause FAILS ON THE BASELINE — a fourth un-dry-run criterion

`GATE.md` §1 records the dry-run as covering *"C1 and its controls N2/N3"*. **C6a was never dry-run
either.** The pre-fix run of the same probe, `logs/08_fixture_selection.log`:

```text
                    PRE-FIX              POST-FIX           change
fuel bal  125       0.4703               0.1505             3.1x better
fuel bal  250       0.5457               0.0283            19.3x better
fuel bal  500       0.0433               0.0063             6.9x better
```

**All three levels fail `< 0.02` on the BASELINE, 500 included.** So the balance clause is a
criterion the unchanged tree cannot satisfy — `CLAUDE.md` §5.2: *"A sanity criterion that the
baseline fails is a broken criterion."* The locator fix improved every level by 3-19x and brought
500 cells inside the bound for the first time. **The clause's failure at 125 and 250 is not a
regression; it is the same un-dry-run defect C5 had, found a fourth time.**

The residual also scales like a discretisation error — `0.52, 0.15, 0.028, 0.0063` is close to
`cells^-2` — so demanding `< 0.02` at 125 cells asks a coarse mesh for a conservation quality the
discretisation does not deliver at that resolution.

### 2.2 The ORDER clause is DIFFERENT: the baseline PASSED it

```text
                    PRE-FIX     POST-FIX    band
p {125, 250, 500}   2.057       2.693       [1.5, 2.5]
                    INSIDE      OUTSIDE
```

**This one is a genuine regression against the frozen criterion.** The baseline satisfied it and the
fixed tree does not. It must not be waved away with §2.1's argument, and it is why C6a is recorded
as FAILED rather than as another broken criterion.

What changed: the fix moved `S_L` at every level, and the successive differences shrank far faster
than before — the difference RATIO went from `3.665e-03 / 8.806e-04 = 4.16` to
`4.336e-04 / 6.704e-05 = 6.47`, i.e. `p` from `log2(4.16) = 2.06` to `log2(6.47) = 2.69`.

**UPDATE 2026-10-09, after TD-029's re-measure: this candidate is WEAKENED and C6a's order
regression is now UNEXPLAINED.** The dt/dx errors were separated on this same tree: spatial
`-0.679 %`, temporal `+0.068 %`, ratio `0.10`. The coupling is real but the temporal part is
**10x smaller** than the spatial, so a combined-refinement order is dominated by spatial
convergence rather than mixing two comparable contributions. It cannot be assumed to account
for 2.057 -> 2.693. See `TD029_error_cancellation/verification.md` §4. The paragraph below is
kept as written, because it is what was believed when C6a was judged.

**A candidate explanation, NOT established.** This family refines `cells`, and the march's `dt` is
diffusion-limited (`dt ~ dx^2`), so refining the mesh refines the time step too — a COUPLED
refinement. TD-005 already recorded that this fixture's spatial and temporal errors have opposite
signs and partially cancel. An observed order above the formal order is a known signature of such
cancellation, and the record's `p = 1.93` came from the EXPENSIVE `{250, 500, 1000}` triplet, not
from this one. So `p = 2.693` may be measuring the coupling rather than a defect — but
**`CLAUDE.md` §6 forbids claiming an observed order whose family is not demonstrably asymptotic, and
that cuts both ways: it also forbids me asserting this explanation without separating dt from dx.**

### 2.3 One thing the fix clearly repaired, visible in the same table

```text
thickness mm   PRE-FIX                      POST-FIX
         63    0.649896                     0.804304
        125    0.625498   <- DECREASES      1.009731
        250    0.536935   <- DECREASES      1.162471
        500    1.228906   <- jumps 2.3x     1.196361
```

Pre-fix the measured flame thickness **fell** from 63 to 250 cells and then jumped 2.3x at 500 — a
physically impossible refinement sequence for a fixed flame, and a direct symptom of the blind
locator mismeasuring the front. Post-fix it rises monotonically toward ~1.196 mm. **Nothing in the
gate asked for this**, and it is the clearest independent evidence in this run that the locator fix
did what A2 claimed.

## 3. THE STOP

```text
The frozen order is C6 -> C6a -> C6b -> C7. C6a FAILS, so the gate STOPS THERE.
C6b and C7 are NOT evaluated and were NOT run. CLAUDE.md §5.3: classifying or explaining a
failure does not waive it.
```

TD-004 now carries **two** independent blockers:

```text
TD-040  the S_L +6.07 % stop, registered in A2 before the run. Unchanged.
C6a     the observed order left the frozen [1.5, 2.5] band, 2.057 -> 2.693.
```

## 4. What is NOT established

```text
NOT established  that the p shift is the dt/dx coupling of §2.2. The discriminator is a refinement
                 that varies dx at FIXED dt, which this family cannot do -- and TD-005's own
                 record says the two errors have opposite signs here, so the coupling is plausible
                 and unproven. Asserting it would be the two-point-order error this campaign has
                 already made once.
NOT established  which eigenvalue is correct. 4.95533 vs 4.86230 is still TD-040's question, and
                 C6's properties 2, 3 and 5 all holding does NOT settle it: a wrong eigenvalue can
                 be perfectly guess-independent, plateaued and convergent.
NOT established  that [1.5, 2.5] is the right band. It was derived around the record's p = 1.93
                 from the EXPENSIVE triplet and applied to a cheaper one that C6a itself introduced.
                 Whether that substitution is legitimate is the question C6a was written to ask,
                 and the answer measured here is "not at this band".
NOT evaluated    C6b and C7. The gate stopped.
NOT run          the §8 regression. Blocked by TD-035 and not warranted on a failing gate.
NOT a fix        anything in this document. No threshold was changed and no production code was
                 touched.
```

**§4's third item is the one that decides what happens next.** C6a's balance clause is a broken
criterion by §5.2 and would need a §5.3 amendment like C5's. Its order clause is a real regression
and needs either a cause or a re-derived band — and those are two different kinds of work, which is
why they are recorded separately rather than as one "C6a FAIL".
