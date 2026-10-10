# TD-005 C3 — PASSES at all 12 configurations. And the consumption identity now exceeds ITS bound.

```text
STATUS:      C3 SATISFIED. 0 of 12 failures against a recorded 5 of 12, worst balance 37x better.
             **But the consumption identity |S_L - S_c|/S_L now exceeds its 0.01 bound at 10 of
             12 configurations, including the reference.** That is C6's own quantity, so C6 remains
             FAILED -- and for a NEW reason. §12 STOP.
SCOPE:       C3 re-measured. C6 is evaluated as a side effect and reported, not amended.
INSTRUMENT:  tools/reproducibility_probe.cpp 94710a235895ec02…, UNMODIFIED, hash-matched to
             logs/00's frozen value. Verified three ways — see §5.
EVIDENCE:    logs/21_c3_remeasure.log   ·  baseline logs/01_reproducibility_baseline.log
             logs/20_c5_controls_remeasure_MISLABELLED_AS_C3.log  (a mis-run, preserved — §5)
TREE:        PremixedFlame.cpp fa815c97… (source gated; the library is recorded, not gated)
COMMIT:      none — not authorized.
```

---

## 1. C3 — PASS, and not marginally

```text
level             PRE-FIX      POST-FIX      C3      delta mm
A window 16    3.6470e-01    6.3030e-03    PASS      1.1964
A window 32    1.5310e-02    6.3030e-03    PASS      1.1964
A window 64    3.6470e-01    6.3030e-03    PASS      1.1964
B anchor 0.30  3.5710e-01    4.5540e-04    PASS      1.1948
B anchor 0.40  1.1940e-02    8.6920e-03    PASS      1.1969
B anchor 0.50  9.0310e-03    1.0040e-02    PASS      1.1975
C relaxT 1.5   2.3800e-01    4.1450e-03    PASS      1.1945
C relaxT 3.0   1.5310e-02    6.3030e-03    PASS      1.1964
C relaxT 6.0   3.7210e-01    8.9470e-03    PASS      1.1971
D tol 1e-02             -    7.9570e-03    PASS      1.1970
D tol 1e-03             -    6.3030e-03    PASS      1.1964
D tol 1e-04             -    6.0950e-03    PASS      1.1965

failures 0 of 12   (recorded 5 of 12)
worst 1.0043e-02 against the 0.05 bound -> inside by 5.0x
recorded worst 3.7210e-01               -> 37x improvement
```

**The bound is untouched.** `0.05` is the repository's own existing precondition from
`TheConsumptionIntegralAgreesWithTheEigenvalue`, and C3 says "neither weakened nor tightened".

### 1.1 The mechanism, visible in the `delta` column

The five recorded failures had `delta` of **0.73-0.85 mm** against **1.23** at the passes, with
`S_c ~3.0` against `~4.9`. They were not marginally worse states — they were a **different
solution**, a thin flame with a low consumption speed.

```text
POST-FIX delta: 1.1945 .. 1.1975 mm at EVERY ONE of the twelve configurations
```

**The thin-flame state no longer occurs anywhere in the sweep.** That is why C3 passes, and it is a
stronger result than the balance numbers alone: the solver now returns the same solution regardless
of the control, which is what an insensitivity sweep is for.

### 1.2 Sweep A confirms TD-037 independently

```text
driftWindow      16          32          64
S_L         4.95533     4.95533     4.95533      spread EXACTLY 0.000000
fuel bal  6.3030e-03  6.3030e-03  6.3030e-03     IDENTICAL
```

The recorded blocker on this sweep was *"4.3 % at windows 32 vs 64"*. It is now **zero to six
decimals, with a bit-identical balance**. TD-037 closed on its own gate; this is an independent
instrument, frozen before that work, agreeing.

## 2. BUT THE CONSUMPTION IDENTITY NOW EXCEEDS 0.01

C6's recorded status is *"identity passes at `0.00777 < 0.01`"*. Re-measured:

```text
level            |S_L - S_c|/S_L     vs 0.01
A window 16/32/64      0.01027       EXCEEDS
B anchor 0.30          0.00769       ok
B anchor 0.40          0.01067       EXCEEDS
B anchor 0.50          0.01053       EXCEEDS
C relaxT 1.5           0.00002       ok      <- essentially EXACT
C relaxT 3.0           0.01027       EXCEEDS     (the reference)
C relaxT 6.0           0.01266       EXCEEDS
D tol 1e-02            0.01176       EXCEEDS
D tol 1e-03            0.01027       EXCEEDS     (the reference)
D tol 1e-04            0.01009       EXCEEDS

10 of 12 exceed. At the reference: 0.01027, against 0.00776 pre-fix (recorded 0.00777).
```

### 2.1 This changes C6's character completely

```text
BEFORE   C6 failed "BY CONSTRUCTION": the identity PASSED at 0.00777 and the test failed on the
         TD-003 ΣY bound (6.29e-07 vs 1e-8). C6's job was to stop the ΣY bound MASKING a passing
         identity.
NOW      the identity itself EXCEEDS 0.01 at the reference. Splitting the test would no longer
         reveal a pass -- it would reveal a second failure.
```

**So C6's premise is obsolete.** It was written to unmask a result that was passing; that result is
no longer passing. Splitting the assertion is still the right thing to do — a masked failure is as
bad as a masked pass — but the expected outcome has inverted, and C6's status row must say so.

### 2.2 And `relaxT/trial 1.5` gives 0.00002, which is the interesting number

```text
relaxT/trial 1.5   S_L 4.89864   S_c 4.89872   mismatch 0.00002
relaxT/trial 3.0   S_L 4.95533   S_c 4.90442   mismatch 0.01027
relaxT/trial 6.0   S_L 4.96877   S_c 4.90587   mismatch 0.01266
```

`S_c` barely moves (4.89872 → 4.90442 → 4.90587, a 0.15 % spread) while `S_L` climbs
(4.89864 → 4.96877, 1.4 %). **The mismatch grows because `S_L` rises away from a nearly-static
`S_c`.** So the identity's failure is about `S_L`, not about the consumption integral — and at short
trials the two agree to five decimals.

**This is NOT established as the cause.** It is a correlation across three levels, and the direction
of causation — whether longer trials push `S_L` up or let `S_c` settle down — is not determined
here.

## 3. Sweep D confirms TD-043's lattice from a probe that predates it

```text
tolerance   reported bracket    TD-043's derived spacing
1e-02       5.430e-03           -
1e-03       6.771e-04           6.771307e-04   <- matches to 4 significant figures
1e-04       8.462e-05           8.4616e-05     <- matches to 4 significant figures
```

TD-043 derived the lattice as `2^(1/2^n) - 1` from three source lines, predicting `8.4616e-05` at
`tolerance = 1e-4`. **This probe, frozen long before TD-043 existed, reports `8.462e-05`.** An
independent confirmation that was not sought.

### 3.1 And it prices TD-043's proposed fix

```text
tolerance   S_L cm/s     change from 1e-3
1e-02       4.96372      +0.169 %
1e-03       4.95533       reference
1e-04       4.95428      -0.021 %
```

Tightening to `1e-4` moves `S_L` by `-0.021 %` and costs `823 s` against `696 s` — about **18 %
more wall time**, not the 30 % I estimated when proposing it. Both numbers are now measured rather
than projected.

## 4. THE STOP

```text
C3   PASSES at all 12 configurations.
C6   the identity it exists to unmask now EXCEEDS its own 0.01 bound at 10 of 12, so C6 remains
     FAILED and its premise is obsolete. TD-005 does not close.
```

**Fourth occurrence of the unmasking pattern** (TD-004 C2 14.66→46.94; TD-032→TD-037; TD-027 C2→C3;
now TD-005 C3→the identity). Each time the correct action made a gate worse by exposing something
underneath. Same response: keep the result, keep the threshold, stop the item, report.

## 5. The instrument, and a mis-run preserved

**I ran the wrong probe first.** I identified C3's instrument as `controls_probe.cpp` from its name
and its presence in `logs/00`'s freeze block; it is TD-005's **C5 negative-controls** probe. My own
inspection of it had already shown `anchorFraction = 0.35` and `tolerance = 1.0e-3` with no sweep
loops — evidence that it sweeps nothing, which I had and did not act on. Cost: ~18 minutes.

Verified three ways for the correct run:

```text
1  logs/01 -- the log C3's status row CITES -- names tools/reproducibility_probe.cpp in its own
   compile line
2  that probe contains the sweep LEVELS {16,32,64}, {0.30,0.40,0.50}, {1.5,3.0,6.0}
3  its hash 94710a235895ec02… matches logs/00's frozen value EXACTLY
```

The mis-run is kept as `logs/20_c5_controls_remeasure_MISLABELLED_AS_C3.log`. It is a **valid C5
re-measure**: the positive control returns `S_L 4.95533` (the post-fix eigenvalue, so it ran on the
right library) and control 2 returns `MaxIterations` with `massFluxResidual 4.1421e-01` as required.
It also surfaced a gap in C5 that nothing had asked about — its own output records that control 1's
construction is **invalid** (`anchorTemperatureFraction = 1.5` is refused), which collapses "no
flame" and "reactions disabled" into the same control, and control 3 was not run. **C5 therefore
has exactly ONE demonstrated control.** That is the probe's conclusion, not mine.

## 6. What is NOT established

```text
NOT established  why the identity mismatch grew. §2.2 shows S_c nearly static while S_L climbs,
                 across three levels. Direction of causation undetermined.
NOT established  that 0.01 is the right identity bound. It is carried over untouched; re-deriving
                 a threshold because the measurement moved is what §5.3 forbids.
NOT re-measured  C1, C2, C4, C5 of TD-005. C5's controls were accidentally re-measured (§5) and
                 behaved, but that is one control of the two the gate needs.
NOT addressed    C6's test split. It requires editing tests/unit/species/test_premixed_flame.cpp,
                 a test change needing its own authorization -- and that file is UNTRACKED, so it
                 carries step 9's delivery problem.
NOT closed       TD-005. C3 passes; C6 fails; C1/C2/C4 unexamined in this run.
```
