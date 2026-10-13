# TD-027 A4 DRY-RUN — the instrument works, non-vacuity holds, and the pre-registered DEVIATION condition FIRED. Reporting before the freeze.

```text
STATUS:      DRY-RUN COMPLETE. Non-vacuity SATISFIED. **A pre-registered invalidation condition
             fired** (proposal §6), so this stops and reports rather than proceeding to freeze.
             C3-A4 FAILS in both shipped configurations, which was predicted.
SCOPE:       measurement only. No production file, no test, no gate, no threshold changed. Nothing
             is frozen. TD-027 remains OPEN on C3.
RUN:         9 h 29 m (34 167 s), four ladder levels plus three single-level B measurements,
             on an idle machine. logs/A4_01_dryrun.log · tools/c3a4_probe.cpp
PROVENANCE:  PremixedFlame.cpp 19db4b6b... (post TD-049 comment fix, comment-only, proven
             bit-identical at object level), libcfdcore.a c127923f..., probe 019a1d7a...
```

---

## 1. THE LADDER, AND THE ONE DEVIATION

```text
level             dt   trial cap     S_L cm/s    published B  bisect    wall s
    0   9.424300e-06       80000     4.954284   8.461627e-05      13      2848
    1   4.712150e-06      160000     4.958058   8.461627e-05      13      5462
    2   2.356075e-06      320000     4.960156   8.461627e-05      13      7991
    3   1.178038e-06      640000     4.960996   8.461627e-05      13     17866

E = measured 0.135289 % + tail 0.016921 % = 0.152210 %
C2-A2 recorded                               0.203139 %   at tol 1e-3
deviation                                    0.050929 %   -> CONDITION FIRED
```

Every level used exactly **13 bisections** and published an identical `B = 8.461627e-05`, which
confirms by measurement that the terminal bracket is deterministic at `2^(1/8192) - 1` for
`tolerance = 1e-4` — the derivation in `audit_C3.md` §2, now observed rather than argued.

## 2. THE DEVIATION IS FULLY EXPLAINED, and it is a finding about C2-A2

Proposal §6 said a material deviation would mean "the recorded C2-A2 value was lattice-limited in a
way not yet understood". It *was* lattice-limited, and it is now understood:

```text
                       increment            relative       in quanta of its own lattice
C2-A2, tol 1e-3   4.962045 - 4.958687   6.767371e-04     1.00 COARSE quanta (q = 6.771307e-04)
A4,    tol 1e-4   4.960996 - 4.960156   1.693208e-04     2.00 FINE   quanta (q = 8.461627e-05)
                                                          -> the tail fell by EXACTLY 4.00x
```

The **measured term agrees to five digits** (A4 `0.135289 %` against C2-A2's `0.1353 %`). The whole
deviation is in the **tail**, which C2-A2 could not resolve below one coarse quantum. So C2-A2's
`E` was inflated by a factor of four *in its tail term only*, exactly the contamination A4 was built
to remove.

### 2.1 C2-A2's verdict is UNAFFECTED, and nothing reopens

```text
C2-A2 budget            0.28 %   (§3.2's labelled empirical constant, untouched)
C2-A2 recorded E        0.203139 %   PASS, margin 1.38x
honest E from A4        0.152210 %   PASS, margin 1.84x
```

**C2-A2 was conservative, not wrong.** Its PASS stands with *more* margin, so no gate flips, no
criterion reopens, and this is a correction forward rather than a regression. Recorded here so the
0.203139 % on the A3 record is read as a conservative bound and not as the temporal error.

## 3. C3-A4's VERDICTS

```text
configuration               tolerance    published B     ratio   verdict   predicted
PRODUCTION  (hpp:196)           1e-04   8.461627e-05    17.988    FAIL      24.007
gated fixture (:188)            1e-03   6.771307e-04     2.248    FAIL       3.000
loose -- the PASS control       5e-03   2.711275e-03     0.561    PASS       0.749
```

**Every predicted ratio was exactly right given the predicted `E`.** Scaling all three by the `E`
correction `0.152210 / 0.203139 = 0.7493` reproduces `17.988`, `2.248` and `0.561` to three
decimals. So the instrument behaved precisely as modelled; the only thing that moved was `E`, and
§2 explains why. That agreement is the strongest evidence here that A4 measures what it claims.

C3-A4 therefore **still fails in both shipped configurations**, as pre-registered. The amendment
makes the measurement honest; it does not make the criterion pass, and it was not proposed in order
to.

## 4. NON-VACUITY — SATISFIED

```text
same instrument, same run       1 PASS, 2 FAIL        -> spans both verdicts
cap 20, the rejection C3 was derived on    58.925     FAIL   reproduced
cap 10, the rejection C3 was derived on     6.232     FAIL   reproduced
```

The two rejections C3 was built to make are reproduced, so A4 is not a weaker instrument than the
one it replaces. (Those two are arithmetic cross-checks against the recorded cap-20 and cap-10
errors, not fresh solves; they are labelled as such in the probe.)

## 5. A CLAIM OF MINE THAT THIS RUN CORRECTS

`amendment_A4_proposal.md` §2 and the commit message for `a0a93a0` both say A4 makes the ratio
**"continuous"**. **That is wrong.** `E` is still an integer number of fine quanta:

```text
E / q(1e-4) = 17.988      measured term 15.99 quanta  +  tail 2.00 quanta
```

What A4 actually does is make the grid **eight times finer** and, decisively, move the threshold
**onto** the grid instead of into a gap:

```text
C3   step 1.3546   reachable 1.3546, 2.7092, 4.0638 ...   threshold 2 UNREACHABLE (in the gap)
A4   step 0.1250   reachable ... 1.7495, 1.8744, 1.9994, 2.1244, 2.2493 ...
                   n = 16 fine quanta -> 1.9994, so the threshold IS attainable
```

The measured `2.248` sits **12.4 % above** the threshold — a near miss, which is a value C3 could
not express at all. That is the design goal met, stated correctly: **not continuity, but a grid fine
enough that the threshold is reachable and a near miss is distinguishable from a gross one.**

The step is `q(tol_meas) / B(tol_judge)`, so it is `1.0` when both tolerances are equal — judging
the production configuration at `tol_meas = 1e-4` gives an integer ratio of 18. The decoupling only
buys resolution when the measuring and judging tolerances **differ**, which is worth stating
plainly because it bounds what A4 can do.

## 6. COST, measured rather than estimated

```text
proposal's labelled estimate   ~4.7 h
MEASURED                        9 h 29 m   (2848 + 5462 + 7991 + 17866 s)
```

The estimate was wrong by 2x and was labelled approximate for exactly this reason. The cause is
visible in the per-level times: the three extra bisections are the **expensive** ones, because
trials nearer the eigenvalue have smaller drift and take longer to clear `driftSettleTolerance`.
Dividing a level's wall time by its march count — which is what I did — assumes every march costs
the same, and they do not.

**Consequence for the next step:** §5.3 step 6 requires a fresh post-freeze run reusing no earlier
result. That is another **~9.5 h** on an otherwise idle machine.

## 7. WHAT IS NOT ESTABLISHED

```text
NOT established  the temporal order p. A4 resolves E 8x better but still does not give an order;
                 the increments are 16 and 2 fine quanta, and TD-043's reach is reduced, not removed.
NOT established  that cap 5 could pass C3-A4. It would need E <= 2*B; at the fixture tolerance that
                 is 16 fine quanta against the measured 18, so a ~12 % reduction in E -- which is
                 within reach of a modestly smaller cap, but sizing it needs the order above.
NOT re-measured  C1, C4, C5, C6, C7. C2-A2's PASS is re-confirmed in §2.1 but was not re-run.
NOT changed      C3, its threshold, the cap, the production tolerance, or any production file.
NOT frozen       GATE_A4. §5.3's order puts the freeze after this report, not before it.
```

## 8. THE DECISION THIS HANDS BACK

The pre-registered rule said to report before proceeding, and the deviation is explained rather than
mysterious, so the instrument is not withdrawn. What remains is a decision:

```text
(a) FREEZE GATE_A4 and run the fresh post-freeze verification (~9.5 h). C3-A4 is then the
    criterion of record, failing at 17.988 (production) and 2.248 (fixture), and TD-027 stays
    open on C3 with an honest instrument.
(b) Also decide the SEPARATE production finding first -- the shipped tolerance 1e-4 publishes a
    bracket 8x finer than the fixture's while carrying the same physical error, which is why
    production reads 17.988 against the fixture's 2.248. That is a production change with its own
    gate and is deliberately outside A4's scope.
```

Both belong to the user. Nothing further is done here.
