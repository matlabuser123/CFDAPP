# TD-005 — DRY-RUN RECORD, written sweep by sweep as the baseline lands

This file exists so that interpretations are recorded **before** the data that would confirm them,
not after. The gate is **not yet frozen**; `logs/01` is the mandatory pre-freeze dry-run
(CLAUDE.md §5.2) and this is its running commentary. Chronology is preserved: nothing here is
revised once the next sweep lands, only appended to.

```text
baseline   logs/01_reproducibility_baseline.log
tree       UNCHANGED, and it CONTAINS TD-004's locator fix
libcfdcore.a  sha256 738f7b555b2a22fb19632eb5b742feda86114dcd6afb2f4bb996c77e342e2303
```

---

## SWEEP A — `driftWindow` ∈ {16, 32, 64} — COMPLETE

```text
      level   S_L cm/s   S_c cm/s  mismatch   fuel bal  delta mm    x_f mm    bracket   steps  march  wall s
  window 16    4.85901    3.04347   0.37364  3.647e-01    0.7365   3.50000  6.771e-04  19013   3973   361.0
  window 32    4.86230    4.90004   0.00776  1.531e-02    1.2289   3.50000  6.771e-04  19264   3968   274.3
  window 64    4.85901    3.04347   0.37364  3.647e-01    0.7365   3.50000  6.771e-04  19909   3973   285.7

  S_L spread (hi-lo)/lo = 0.000677    worst fuel balance = 3.6467e-01    worst mismatch = 0.37364
```

### A.1 Criterion verdicts on the unchanged tree

| criterion | measured | verdict |
| --- | --- | --- |
| C1 bitwise `flameSpeed == massFlux/unburntDensity` | — | pending the probe's close |
| C2 bracket honesty | `6.771e-04 <= 1e-3` at every level | consistent so far |
| **C3** `fuelBalanceResidual < 0.05` at EVERY level | **`0.3647` at windows 16 and 64**; `0.0153` at 32 | **FAILS — as predicted, and this is F1** |
| existing test's bound, `S_L` spread `< 0.02` | `0.000677` | PASSES — which is exactly the fail-open |

**C3's non-vacuity is now proved in both directions by the baseline alone**, with no constructed
control: the same criterion passes at window 32 and fails at windows 16 and 64. That is the
strongest form of non-vacuity §5.2 asks for.

**And the fail-open is now quantified.** The in-tree test bound passes with a spread of `6.8e-04`
while two of the three configurations behind it are 24x outside the tree's own admissibility bound.
`TheAnswerIsInsensitiveToTheStepsPerTrial` is not merely incomplete; on this fixture it passes while
**two thirds** of its sample is inadmissible.

### A.2 The result that refutes audit §9's reading (i)

Audit §9 proposed that the bad mode is a trial reading its drift before the structure relaxed, and
nominated the discriminator: the bad mode appeared at the SHORTER trial, so more relaxation should
remove it. **Sweep A refutes that directly:**

```text
window 16  (shortest)  -> BAD   0.3647
window 32  (middle)    -> GOOD  0.0153
window 64  (longest)   -> BAD   0.3647
```

Non-monotonic. The longest trial lands in the bad mode too, so "not relaxed enough" cannot be the
mechanism. Audit §9 reading (i) is **refuted**; it stands in that file as written, per §1.5.

### A.3 What the numbers say instead, and it is exact

Windows 16 and 64 are **identical to six figures** in every reported quantity — `S_L`, `S_c`,
`mismatch`, `fuel balance`, `delta`, `march` — while their step counts differ (19013 vs 19909). So
the knob genuinely moved the solve, and the solve nonetheless landed in one of exactly **two**
discrete final states. This is bimodality, not a spread.

The two modes are separated by **exactly one bisection**:

```text
(S_L_good - S_L_bad)/S_L_bad  =  6.770927e-04
reported bracket residual     =  6.771000e-04
one geometric bisection       =  2^(1/1024) - 1  =  6.771307e-04
ratio                         =  1.0000
```

and the bad mode is the **LOWER** `mdot`. `PremixedFlame.hpp:52-56` states what a too-low `mdot`
does, and it was written long before this measurement:

> *"too small an mdot and the front is driven back into the inlet, where the Dirichlet condition
> quenches it"*

### A.4 A THIRD reading, stated as a hypothesis

```text
(iii) The final march is run at the bracket's geometric CENTRE, and the bracket -- 0.068 % wide,
      exactly as documented -- is wide enough that the LOWER of two adjacent centres drives the
      front upstream into the inlet over a ~3970-step march. So the reported STATE is
      hypersensitive to the eigenvalue's own ADMITTED uncertainty, while S_L is not.
```

This is consistent with every number in sweep A, with the header's cold-boundary statement, and with
TD-004's conclusion that its 164-cell shifts had been *masking* a flashback (TD-004's fix now refuses
those shifts, so the front runs away instead of being dragged back). **It is not yet established.**

### A.5 THE PREDICTION, pre-registered before sweep D runs

Sweep D varies `tolerance` ∈ {1e-2, 1e-3, 1e-4}, which is precisely the width of the bracket that
reading (iii) blames. It is already in the running probe, and its rows have not been seen. So:

```text
IF reading (iii) is right:
  tol 1e-2  (bracket ~10x WIDER)  -> the bad mode should be AT LEAST as likely; the residual
                                     drift is ~10x larger, so flashback is easier
  tol 1e-4  (bracket ~10x TIGHTER)-> the bad mode should DISAPPEAR, and the fuel balance should
                                     come in near 0.0153 or better
IF reading (iii) is wrong:
  the bad mode appears at tol 1e-4 as well, and the bracket width is not what selects the mode.
```

**Falsification is explicit:** a bad mode at `tol = 1e-4` refutes (iii). This is written down now,
with the rows unseen, so that whichever way sweep D falls it is a test and not a story fitted
afterwards — which is the failure mode TD-004 hit five times.

### A.6 What this does NOT yet decide

Whether the bimodality is TD-005's to fix or TD-004's remains open. Reading (iii) would make it
**TD-005's**, because the bracket width and the final march's duration are both extraction-method
parameters. But a flashback is TD-004's unresolved blocker, and if sweep D refutes (iii) then C3
fails for a reason that is already preserved under TD-004 and **TD-005 stops at C3**.

---

## SWEEP B — `anchorFraction` ∈ {0.30, 0.40, 0.50} — COMPLETE

```text
      level   S_L cm/s   S_c cm/s  mismatch   fuel bal  delta mm    x_f mm    bracket   steps  march  wall s
anchor 0.30    4.85572    3.03387   0.37520  3.571e-01    0.7345   3.00000  6.771e-04  19275   3979   263.1
anchor 0.40    4.86230    4.90129   0.00802  1.194e-02    1.2300   4.00000  6.771e-04  19264   3968   302.5
anchor 0.50    4.86230    4.90185   0.00813  9.031e-03    1.2299   5.00000  6.771e-04  19264   3968   350.5

  S_L spread (hi-lo)/lo = 0.001355    worst fuel balance = 3.5705e-01    worst mismatch = 0.37520
```

### B.1 C4 PASSES BOTH OF ITS MEASURABLE CLAUSES, and that is the first good news in this item

| clause | threshold | measured | verdict |
| --- | --- | --- | --- |
| C4(b) `S_L` spread over the three anchors | `<= 0.02` (existing in-tree bound) | **0.001355** | **PASS**, 15x inside |
| C4(c) successive differences non-increasing | `|S(0.50)-S(0.40)| <= |S(0.40)-S(0.30)|` | `0.0` vs `6.77e-04` | **PASS** |

C4(c) passes in its strongest possible form: `S_L` at anchors 0.40 and 0.50 is **identical to six
figures** (`4.86230` both), so the successive difference is **exactly zero**. The plateau is not
merely within a tolerance — it is flat.

**So Finding 2 is a MEASUREMENT gap, not a physics failure.** The plateau that
`PremixedFlame.hpp:52-56` claims selects the eigenvalue is real and is now measured. What was
missing was only the measurement, and the test that would have produced it was recorded as a
timeout at a cap that was not the repository's (GATE §4.4).

### B.2 The cold-boundary trend is visible in the balance, and it is monotone

```text
anchor   x_f      upstream room        fuel balance
 0.30    3.0 mm   2.44 delta_preheat     3.571e-01     <-- flashback
 0.35    3.5 mm   2.85 delta_preheat     1.531e-02     (the default fixture, sweep A window 32)
 0.40    4.0 mm   3.26 delta_preheat     1.194e-02
 0.50    5.0 mm   4.07 delta_preheat     9.031e-03
```

Monotone improvement with upstream room, exactly as the cold-boundary difficulty predicts, and the
**default fixture at 0.35 sits just inside the admissible region**. Anchor 0.30, at 2.44 preheat
thicknesses, is too close to a 300 K Dirichlet inlet and flashes back. For that row the flashback has
a straightforward *physical* explanation and is not evidence of a method defect.

**This does NOT explain sweep A**, and the distinction matters. Windows 16, 32 and 64 all ran at
anchor 0.35 — identical upstream room — and two of them flashed back while one did not. There the
only difference is which rung of the bisection ladder `mdot*` landed on, so §A.4's reading (iii)
remains the live hypothesis for sweep A and §A.5's prediction stands untouched.

### B.3 The ladder now has three rungs, and only the top one is admissible

```text
4.85572   anchor 0.30                 BAD   0.3571      two bisections below the top
4.85901   windows 16 and 64           BAD   0.3647      one bisection below the top
4.86230   window 32, anchors .40 .50  GOOD  0.0090-0.0153

step 0.30 -> 16/64 : 1.0006 bisections
step 16/64 -> 32   : 0.9999 bisections
```

Every observed `S_L` is a single-bisection step from its neighbour, and admissibility appears only at
the top rung. A flashback threshold lying **inside one bracket width** of the eigenvalue would produce
exactly this. Sweep D tests that.

### B.4 A caution against the obvious conclusion

It is tempting to read B.3 as "the search sometimes lands one rung too low, so tighten the bracket
and the problem goes away". That is §A.4 reading (iii) and it is **not yet established** — the three
rungs are confounded with three different *configurations*, and a lower rung may be the correct
eigenvalue for its configuration rather than an error. What separates the two is sweep D, which
varies ONLY the bracket width at a fixed configuration. The prediction is already recorded in §A.5
with the rows unseen.

## SWEEP C — `relaxationTimesPerTrial` ∈ {1.5, 3.0, 6.0} — COMPLETE

Sweep A had already weakened this sweep's role: §A.2 refuted the relaxation reading, so C became a
measurement of a knob no test has ever varied rather than audit §9's decisive experiment.

```text
            level   S_L cm/s   S_c cm/s  mismatch   fuel bal  delta mm    x_f mm    bracket   steps  march  wall s
 relaxT/trial 1.5    4.76455    3.59953   0.24452  2.380e-01    0.8501   3.50000  6.771e-04  12389   4133   240.4
 relaxT/trial 3.0    4.86230    4.90004   0.00776  1.531e-02    1.2289   3.50000  6.771e-04  19264   3968   382.5
 relaxT/trial 6.0    4.86888    3.01833   0.38008  3.721e-01    0.7311   3.50000  6.771e-04  32862   3957   643.6

  S_L spread (hi-lo)/lo = 0.021897    worst fuel balance = 3.7213e-01    worst mismatch = 0.38008
  guard passed: the independent variable moved the solve.
```

### C.1 The spread EXCEEDS the in-tree insensitivity bound

```text
S_L spread                    2.1897 %
the bound the other two insensitivity tests use   2.00 %   -> EXCEEDED
G8's band half-width                              2.80 %   -> the spread is 78 % of it
```

**This is Finding 4 turning from a coverage gap into a measured defect.** The knob introduced to fix
the recorded 4.3 % blocker produces, on its own, a spread that would FAIL the criterion the other
two knobs are held to — and no test has ever varied it.

### C.2 But "2.19 % imprecision" is the WRONG reading, and the distinction is load-bearing

The eigenvalue is **converging** in this knob:

```text
relaxT 1.5 -> 3.0 :  2.05161e-02   =  30.3 bisections
relaxT 3.0 -> 6.0 :  1.35327e-03   =   2.0 bisections      a 15x contraction
```

So the sequence is settling toward ~`4.8689 cm/s`, and the default (3.0) sits `0.135 %` below that.
The 2.19 % spread is **dominated by `relaxT 1.5`, which is simply not converged**. A convergence
requirement swept below its own convergence point measures non-convergence, not insensitivity.

**Whether an unconverged level belongs in an insensitivity sweep is a GATE-DESIGN question**, and it
must be settled by derivation rather than by whichever choice passes. Recorded here, unresolved, so
that the answer is not quietly chosen later to suit the result.

### C.3 The fail-open defect is sharper than audit §4 stated

```text
relaxT 6.0   the BEST-converged eigenvalue (4.86888)   the WORST state (balance 0.3721)
relaxT 3.0   0.135 % below it                          the only admissible state (0.0153)
```

**Converging the eigenvalue makes the reported solution worse.** Admissibility is non-monotonic in
the knob, so no "use a larger value" rule fixes it. This is also the **second independent refutation**
of "more relaxation removes the bad mode" — §A.2's non-monotonicity was the first.

---

## SWEEP D — `tolerance` ∈ {1e-2, 1e-3, 1e-4} — COMPLETE. §A.5's PREDICTION IS REFUTED.

```text
        level   S_L cm/s   S_c cm/s  mismatch   fuel bal  delta mm    bracket     steps  march  wall s
    tol 1e-02    4.85736    4.89991   0.00876  9.696e-03    1.2289  5.430e-03     15624   3976   297.7
    tol 1e-03    4.86230    4.90004   0.00776  1.531e-02    1.2289  6.771e-04     19264   3968   330.6
    tol 1e-04    4.86086    4.90012   0.00808  8.996e-03    1.2291  8.462e-05     22914   3970   291.3

  S_L spread = 0.001016    worst fuel balance = 1.5314e-02    worst mismatch = 0.00876
  guard passed: the independent variable moved the solve.
```

### D.1 Every level is admissible, over a 100x range of bracket widths

**No bad mode appears anywhere in sweep D.** The worst fuel balance is `1.5314e-02`, inside C3's
`0.05` at all three levels. So varying **only** the bracket width, at a fixed configuration, across
two decades, never produces the bad mode.

### D.2 Reading (iii) is refuted — and NOT by the falsifier I named

§A.5 pre-registered two directions and named only one as the falsifier. Honest accounting:

| §A.5 predicted | measured | verdict |
| --- | --- | --- |
| `tol 1e-2`, a ~10x WIDER bracket: the bad mode "should be AT LEAST as likely" | `fuel bal 9.696e-03` — the **best** state at this anchor | **contradicted** |
| `tol 1e-4`, a ~10x TIGHTER bracket: the bad mode "should DISAPPEAR" | no bad mode | consistent, but vacuously — there was none to remove |
| named falsifier: "a bad mode at `tol = 1e-4` refutes (iii)" | did not occur | **the falsifier did not fire** |

So **reading (iii) is dead**, killed by the wider-bracket direction, which is the direction I did
**not** nominate as the falsifier. **My pre-registration was incomplete:** I named the weaker of the
two tests. The correct practice is to name a falsifier for *each* direction a prediction commits to,
because a one-sided falsifier can leave a refuted hypothesis looking merely unconfirmed. Recorded
rather than tidied, because the discipline is the point.

### D.3 C2 PASSES, with derived bounds and real margin

```text
C2a  bracket honesty, reported <= requested
       tol 1e-2  5.4300e-03 <= 1e-2   OK
       tol 1e-3  6.7710e-04 <= 1e-3   OK
       tol 1e-4  8.4620e-05 <= 1e-4   OK

C2b  successive tightenings against the sqrt(1+t)-1 bound derived in GATE 4.2
       |S(1e-3)-S(1e-2)|/S = 1.0170e-03   bound 5.4874e-03   PASS  (5.4x margin)
       |S(1e-4)-S(1e-3)|/S = 2.9616e-04   bound 5.4987e-04   PASS  (1.9x margin)
```

The bracket is an honest bound on the eigenvalue, and the eigenvalue converges as it tightens. **The
eigenvalue SEARCH is sound.** What is not sound is the state it hands back.

---

## CROSS-SWEEP: WHAT ACTUALLY SELECTS THE BAD MODE — STILL UNKNOWN

```text
BAD  (balance > 0.2)                                          delta mm
  window 16      anchor .35  relaxT 3.0  tol 1e-3   0.3647      0.7365
  window 64      anchor .35  relaxT 3.0  tol 1e-3   0.3647      0.7365
  anchor 0.30    window 32   relaxT 3.0  tol 1e-3   0.3571      0.7345
  relaxT 1.5     window 32   anchor .35  tol 1e-3   0.2380      0.8501
  relaxT 6.0     window 32   anchor .35  tol 1e-3   0.3721      0.7311

GOOD (balance < 0.02)
  window 32 / anchor .35 / relaxT 3.0, at tol 1e-2, 1e-3, 1e-4  0.0090-0.0153   1.2289-1.2291
  anchor 0.40, anchor 0.50 (window 32, relaxT 3.0)              0.0119, 0.0090  1.2300, 1.2299
```

**`delta` is a perfect discriminator**: every admissible state has `delta ~ 1.229 mm`, every
inadmissible one `0.731-0.850 mm`. Two attractors, cleanly separated.

**Three hypotheses are now refuted:**

```text
(i)   insufficient relaxation          refuted twice -- A.2 (window 64 is the LONGEST and bad)
                                                        C.3 (relaxT 6.0 is the MOST relaxed and bad)
(ii)  inlet proximity                  explains anchor 0.30 ONLY; windows 16 and 64 sit at the
                                       same anchor as the good window 32
(iii) bracket width selects the mode   refuted by D.1/D.2 over a 100x range
```

**The one regularity left, and it is uncomfortable:** admissibility coincides with the library's
**default** `driftWindow = 32` and `relaxationTimesPerTrial = 3.0`, and in BOTH sweeps the state
breaks when the knob moves in **either** direction. An isolated island of admissibility at the
default values is the opposite of the insensitivity the header requires, and it raises a question
this dry-run cannot answer: whether those defaults were chosen, at some earlier point, because they
are the values that work on this fixture.

**NO FOURTH HYPOTHESIS IS OFFERED HERE, deliberately.** Every measurement in this file is a counter
derived from the temperature profile, and TD-004 burned five hypotheses that way — its own summary
§7.4 named the remedy and never applied it. The next step that is worth trusting is to **dump the
temperature and fuel fields for one good and one bad configuration and compare them directly**, which
is the first instrument in this chain that is not downstream of the unknown.

---

## DRY-RUN STATUS AGAINST THE GATE

| # | verdict on the unchanged tree | non-vacuity |
| --- | --- | --- |
| C1 | **NOT EVALUATED — a gap in this probe**, see below | — |
| C2 | **PASS** (§D.3) | would fail on a dishonest bracket or a non-converging ladder |
| C3 | **FAIL at 5 of 12 configurations** (`0.2380 … 0.3721`) | **proved in BOTH directions by the baseline alone** — 7 of 12 pass, 5 fail, same criterion |
| C4(b) | **PASS** `0.001355 <= 0.02` | a spread above 0.02 fails |
| C4(c) | **PASS**, exactly 0 vs `6.77e-04` | a growing difference fails |
| C4(a) | pending `logs/02` at `--timeout 7200` | — |
| C5 | pending `logs/02` / `logs/03` | — |
| C6 | FAIL by construction (audit §6) | — |
| C7 | pending | — |

### A GAP IN THIS PROBE, recorded rather than worked around

**C1 cannot be evaluated from `logs/01`.** The criterion is
`flameSpeed == massFlux / unburntDensity` bitwise, and the probe prints `flameSpeed` but **not**
`massFlux` or `unburntDensity`. The dry-run for C1 is therefore **missing**, not passing — exactly
the distinction CLAUDE.md §5.2 draws when it says a clause that was never executed is not
"passing". It needs one more field in the probe and a re-run of a single configuration, which is
cheap and must happen before the freeze.

---

# FIELD DUMP (`logs/02`) — ROOT CAUSE FOUND, AND IT IS NOT WHAT ANY OF THE THREE HYPOTHESES SAID

Two solves differing **only** in `driftWindow`, with the temperature, fuel, velocity and
heat-release fields printed rather than reduced to counters. No hypothesis was offered beforehand;
the probe's header states the three places the missing fuel could go and prints the two numbers that
decide between them.

## E.1 Neither candidate explanation survives

```text
                        inlet (T[0]-T_u)/(T_ad-T_u)      outlet Y_F[nc-1]/Y_F,u
GOOD (window 32)              1.038037e-04                    3.984806e-16
BAD  (window 16)              1.232900e-04                    3.071104e-18
```

**No flashback** — the inlet sits at 300.18 K, four orders below the full rise. **No breakthrough**
— the outlet fuel is at round-off. The recentring ledger is *identical* in the two runs
(`-2.471500e-05` vs `-2.471478e-05`). Both modes reach `T_ad = 1800.00 K` exactly and consume their
fuel. **Both are physically sensible flames.**

This also **contradicts the flashback reading** that TD-004's summary carried as its remaining
blocker, for the final state. Corrected there, in place.

## E.2 What the fields actually show: the front is in a different PLACE

```text
GOOD  peak heat release at x = 4.01 mm   (T = 1593 K)   preheat visibly broader
BAD   peak heat release at x = 2.81 mm   (T = 1506 K)   front narrower, 1.2 mm upstream
```

Interpolating each profile for the `T_anchor = 1050 K` crossing that **defines** the front:

```text
                   real crossing      offset from the 3.500 mm anchor
GOOD (window 32)      3.506 mm            +0.006 mm  =   +0.3 cells
BAD  (window 16)      2.537 mm            -0.963 mm  =  -48.2 cells      16x the dead band

result.flamePosition reported in BOTH cases:   3.50000 mm
```

**The good mode's front is at the anchor to a third of a cell** — so the mechanism works when it
works. **The bad mode's front is 48 cells upstream**, and the reported front position is the same
number in both.

## E.3 THE DEFECT, in one line of production code

```text
src/species/PremixedFlame.cpp:1725      result.flamePosition = xAnchor;
```

That is the **sole** assignment to `result.flamePosition` in the whole file. It assigns
`xAnchor = anchorFraction * domainLength` — **a pure input**. The field is never measured, and the
header documents it as something it is not:

```text
PremixedFlame.hpp:  Real flamePosition{};   // x where T crosses T_anchor [m]
```

### E.3.1 And the test that was supposed to catch it compares an input to itself

```text
test_premixed_flame.cpp:400
    EXPECT_NEAR(flame.flamePosition, fraction * settings.domainLength,
                0.02 * settings.domainLength);
```

With `flamePosition := anchorFraction * domainLength` and `fraction == anchorFraction`, this asserts

```text
    anchorFraction * domainLength  ~=  anchorFraction * domainLength
```

**an identity that cannot fail for any solver behaviour whatsoever** — including a solver that never
ran. Its own comment claims the opposite: *"The controller really did hold the front where it was
told to."* This is the most complete fail-open instrument this campaign has found: not a probe with a
disconnected input, but a check whose two sides are the **same expression**.

## E.4 The causal chain, now field-grounded

```text
1. the front drifts ~48 cells upstream of the anchor         (logs/02, measured)
2. correcting that needs a 48-cell translation, which TD-004's admissible-jump bound
   (~5.1 cells) correctly REFUSES as unphysical for one step
3. so no recentring occurs and the front stays off-anchor permanently
4. result.flamePosition reports the ANCHOR regardless, so nothing in the suite sees step 1
5. the fuel balance, computed from the real field, reports ~37 % -- it is the ONLY
   instrument in this chain that was ever telling the truth
```

**This reframes TD-004's refusal counters.** TD-004 measured 4 441 and 16 347 refused crossings and
read them first as "the profile has multiple crossings" (refuted by `maxFrontCrossings = 1`) and then
as flashback. The simpler reading, consistent with every number in both items: **the front really had
moved tens of cells, the locator was right to refuse to teleport the frame to it in one step, and
nothing then corrected the situation** — while the reported position concealed it.

## E.5 Evidence contaminated by E.3, including this file's own tables

Every `x_f` / `x_front` column in `logs/01`, in this file's sweep tables, in `audit.md`, and in
**all** of TD-004's logs is `anchorFraction * domainLength` echoed back. It is not a measurement and
must not be read as one. Specifically:

```text
logs/01 sweep B "x_f mm" 3.00000 / 4.00000 / 5.00000   -- the three anchors, echoed
TD-004 verification.md C5  "x_front 3.50000 exactly"   -- vacuous as evidence that
                                                          "what already works is preserved"
```

Marked rather than deleted. The columns stay in place so the chronology is intact.

## E.6 C1 PASSES, measured — the gap in `logs/01` is closed

```text
GOOD  flameSpeed      4.86229685763685318e-02
      massFlux/rho_u  4.86229685763685318e-02     BITWISE EQUAL: YES
BAD   flameSpeed      4.85900667507934217e-02
      massFlux/rho_u  4.85900667507934217e-02     BITWISE EQUAL: YES
```

**A pointed contrast worth naming:** C1 — the criterion that looked like pedantry when it was derived
— passes, while the *reported front position* next to it was an echoed input all along. C1 checked
the one relation that happened to be honest. A bitwise identity between two reported fields proves
they are consistent; it cannot prove either is measured. **C4 therefore needs a criterion that
compares a reported quantity against the FIELD**, which is added to the gate.

## E.7 What is established, and what is not

```text
ESTABLISHED
  * result.flamePosition is assigned xAnchor and never measured          (code, sole assignment)
  * the plateau test's front-position clause is an identity             (code)
  * in the bad mode the real 1050 K crossing is 48.2 cells off-anchor   (field)
  * in the good mode it is 0.3 cells off-anchor                         (field)
  * neither mode flashes back or leaks fuel                             (field)
  * C1 holds bitwise in both                                            (field)

NOT ESTABLISHED
  * WHY the front settles 48 cells upstream in the bad mode
  * whether the off-anchor front fully accounts for the 37 % fuel balance
  * whether fixing the REPORT changes any answer -- it should not, and that is a
    criterion, not an assumption
```

**No fourth hypothesis about the 48-cell drift is offered here.** The honest position is that the
*report* is broken, that is fixable and verifiable on its own, and the drift must be re-diagnosed
with an instrument that can see it — which does not exist until the report is fixed.

---

# C3 DIAGNOSED (`logs/10`, `logs/11`) — AN UNSTABLE FRONT, NOT A CORRUPTED STATE

The three refuted hypotheses all assumed the inadmissible state was **insufficiently converged**.
It is a state that **converged and then departed**, and the mechanism is measured end to end.

> ## TD-032 — EVERYTHING FROM HERE TO §N RESTS ON A FIELD THAT ALTERNATES EVERY STEP
>
> **Recorded 2026-10-06. Nothing below is deleted; the measurements stand and the models built on
> them do not.**
>
> `TD031_final_march_length/logs/05_cell35_history.log` stopped the final march at five consecutive
> budgets and found the reported field alternating **every single step** between the cold preheat
> zone (~310 K) and a nearly fully burnt field (~1790 K). Root cause, found by reading
> `src/species/PremixedFlame.cpp:1189-1191`: the transient term's old time level is copied BEFORE
> the state is updated, so it trails by one step from step 2 onward; the transient path then
> connects step `k-1` to step `k+1` and the even and odd steps become two interleaved sub-sequences
> that need not share a fixed point. Audit:
> `technical_debt/TD032_lagged_time_level/audit.md`.
>
> **Consequence for §G–§N.** Every good/bad classification in these sections was taken from the
> reported field at one march budget, and the budget's PARITY determines which branch is reported.
> All 30 such verdicts across these ladders are predicted by parity alone
> (`TD031_final_march_length/PARITY_FINDING.md` §2). So:
>
> * the **period-14 limit cycle** (§I.2, §I.4) does not exist — it was fitting the parity of ~397
>   steps per relaxation time, where integer rounding makes it effectively arbitrary along a ladder;
> * the **period moving with the mesh** (§K.2) is the parity changing with `dx`, not a period;
> * the conclusion that the oscillation is **NOT physical** (§J.4) is CONFIRMED, and by a mechanism
>   rather than by an argument from `Ze(Le-1) = 0`;
> * §J.3's **"the eigenvalue is not affected"** is NARROWED, not upheld: `runTrial` calls the same
>   `advance()`, so the trials carry the same lag. What survives is that `S_L` is read from the
>   drift's SIGN and converges in `dt` to a Richardson limit — evidence the sign is robust, not
>   evidence the march is correct. See TD-032 audit §5.
>
> **What is unaffected:** §E (C4d, the echoed `flamePosition`, verified bitwise by two libraries) and
> §L.2's `S_L`-moves-with-`dt` result, which carved out TD-027.

## G.1 The trajectory, with the departure bracketed

`finalMarchRelaxationTimes` scales only the final march, and the solver is deterministic, so the
state entering the march is identical across these rows: this is a time series.

```text
driftWindow 16 (mdot = 4.85901, one bisection BELOW the good mode)
relaxT  steps   offset     d(offset)/397 steps   ratio    fuel bal    S_c      delta    rejects
 4.00   1589    +1.50                                    1.993e-02  4.74706  1.1496      0
 5.00   1986    +1.21        -0.29                       1.837e-02  4.75278  1.1522      0
 6.00   2384    +0.92        -0.29        1.00x           1.619e-02  4.76217  1.1568      0  <- BEST
 7.00   2781    -1.13        -2.05        7.09x           2.878e-02  4.70013  1.1290      0
 8.00   3179    -7.80        -6.65        3.25x           6.409e-02  4.52177  1.0656     73
10.00   3973   -46.58       -19.39        2.91x           3.647e-01  3.04347  0.7365    470

driftWindow 32 (mdot = 4.86230) over the SAME ladder: +1.0 .. +1.2 cells throughout. Never departs.
```

## G.2 What is ESTABLISHED by measurement

```text
1. while the front is within ~1.5 cells of the anchor it drifts upstream at a CONSTANT rate --
   -0.29 cells per 397 steps over three consecutive intervals, ratio exactly 1.00x
2. it CROSSES the anchor between 2384 and 2781 steps
3. after crossing, the displacement grows ~3x per 397 steps: EXPONENTIAL, fitted e-folding
   ~530 steps (~50 /s at dt = 3.77e-5 s)
4. the fuel balance, S_c and delta track the displacement MONOTONICALLY and are at their best
   at the moment the offset is smallest (relaxT 6: +0.92 cells, balance 1.619e-02)
5. rejected crossings are EXACTLY ZERO until |offset| exceeds the 3-cell recentring dead band,
   then 73 and 470
6. the PROFILE IS A TEXTBOOK FLAME AT BOTH ENDS -- smooth monotone T from 300 to 1800 K, a
   single clean heat-release peak, Y_F to round-off, u through the full 6x expansion. The
   departed state is the SAME FLAME TRANSLATED ~46 cells upstream and slightly compressed
   (delta 1.157 -> 0.737 mm)
7. the good mode, differing ONLY in mdot by one bisection, does not depart within 3973 steps
```

**So the inadmissible state is not a corrupted profile and not a conservation defect in the
discretisation.** It is a correct flame in the wrong place, still moving — and the fuel-balance
residual is correctly reporting that the state is **unsteady**, which is exactly what a steady
balance should do when the front is travelling at 0.049 cells/step.

## G.3 The mechanism — **INVALID AS AUTHORITATIVE.** Falsified by `logs/13`.

> **THREE OF THE FOUR CLAIMS BELOW ARE REFUTED BY THE FALSIFICATION TESTS THEY THEMSELVES
> SPECIFIED** (G.5 → `logs/13`). The reading is kept in place per CLAUDE.md §4; §H carries the
> replacement. What was refuted, and by which test:
>
> ```text
> (c) "past the anchor it runs away EXPONENTIALLY"
>     FALSIFIED by Test B. The good mode is bad at relaxT 20 and 30 and GOOD AGAIN at 40, with
>     the best fuel balance in the whole item (1.049e-02). An exponentially unstable front
>     cannot return. What I measured as exponential growth over relaxT 4..10 in logs/11 was the
>     RISING LIMB OF A HALF-CYCLE; locally it fits an exponential, and I fitted one.
> (d) "the +-3 cell dead band gives the growth a free window"
> (e) "once the band is exceeded the needed shift is already refused"
>     FALSIFIED by Test A. Over a 10x dead-band range at identical mdot the offset is flat to
>     2 % (-46.55 / -46.58 / -45.68 cells) and the balance is unchanged. Band 1.0 performed
>     252 recentrings against 7 at the default and changed nothing.
> (f) "every mdot in the bracket eventually departs"
>     FALSIFIED AS STATED by Test B: the good mode does not depart at relaxT 40.
> (b) "with mdot one bisection low it creeps upstream"
>     NOT SUPPORTED. Test A's band-6.0 row landed on a LOWER mdot (4.80016 vs 4.85901) and gave
>     a GOOD state. Confounded by the band change, so this is a mark against (b), not a
>     refutation of it.
> (a) "an ill-conditioned equilibrium"
>     NOT TESTED. The oscillation in §H is consistent with it but does not require it.
> ```

```text
(a) the front sits at an ILL-CONDITIONED equilibrium. PremixedFlame.hpp:63-65 says so, long
    before any of this was measured: "the plateau means mdot(x_f) is nearly FLAT near the
    eigenvalue, so its inverse x_f(mdot) is nearly VERTICAL."
(b) with mdot one bisection low the front creeps upstream linearly while near the anchor (G.2.1)
(c) past the anchor it runs away exponentially, e-folding ~530 steps (G.2.3)
(d) the +-3 cell DEAD BAND means recentring never engages during the free-growth window
(e) by the time |offset| exceeds the dead band, the shift needed already exceeds the ~5.1-cell
    admissible-jump bound, so the correction is REFUSED -- 73 then 470 rejections (G.2.5)
(f) the front runs out; the balance reports the resulting unsteadiness
```

**Consistency check on (c):** from relaxT 6 to 10 is 1589 steps = 3.0 e-foldings, so `0.92 cells`
should become `~18 cells`; measured `46.6`. Same order, right sign, right shape — the exponential
reading is consistent to a factor of ~2.5 and is not a fit to a single interval.

## G.4 The C2 unification — **INVALID AS AUTHORITATIVE in its mechanism, CORRECT in its claim**

> The claim that C2 and C3 are ONE mechanism **stands and is strengthened** — `logs/13` reproduces
> TD-004's C2 measurement to every digit. But the mechanism named below is wrong: it is **not a
> runaway**, so G.4.1's conclusion that "no achievable bracket is tight enough for an arbitrarily
> long march" does not follow from a growth rate. §H restates both.

TD-004 stopped at C2 because the fuel balance **degraded with march length**:

```text
finalMarchRelaxationTimes   10        30        90
fuel balance                0.0153    0.6022    0.7188     (TD-004 verification.md)
```

That is **this same runaway**, at the *good* mdot, with a later onset. A longer march buys more
e-foldings, so every mdot in the bracket eventually departs. **One mechanism explains both failed
criteria in both items**, which is why neither could be fixed from inside its own framing.

### G.4.1 The design consequence, which is the part that matters

The bracket width and the march length are **coupled by the growth rate**, and the current design
treats them as independent:

```text
amplification over a march of N steps  ~  exp(N / 530)
N = 3973  ->  e^7.5  ~  1.8e3
```

So **no achievable bracket is tight enough for an arbitrarily long march**, and tightening
`tolerance` cannot fix C3 — which is exactly what sweep D measured, with no bad mode at any bracket
width over a 100x range, because at `relaxT 10` the amplification is the same for all of them.
Three ways out, none of them this item's to choose:

```text
* bound the final march by the e-folding time rather than by relaxation times
* make the front actively HELD -- which is what recentring is for, and what the dead band and
  the jump bound together disable precisely when it is needed (d, e)
* change the formulation so the front is not at an unstable equilibrium (route (b)'s
  drifting frame, or TD-005's own eigenvalue reformulation)
```

## G.5 How to FALSIFY G.3, written down before anyone acts on it

```text
* deliberately displace the seeded front by +-1, +-2, +-5 cells at the converged mdot and march:
  the instability reading predicts the displacement GROWS with e-folding ~530 steps in both
  directions, and predicts nothing else. If small displacements DECAY, the equilibrium is stable
  and G.3 is wrong.
* widen the recentring dead band to 0 cells (recentre every step): (d) predicts the departure is
  suppressed or much delayed. If it departs identically, the dead band is not implicated.
* march the GOOD mode past ~7 e-foldings: G.4 predicts it departs too. TD-004's relaxT 30/90
  rows already suggest it does, and a direct run would confirm it.
```

**None of these is a fix, and none is authorized.** They are the experiments that would promote
G.3 from a well-supported reading to an established mechanism.

## G.6 A probe defect in `logs/10`, fixed in `logs/11`

`profileAroundFront` looped `for (Index k = -60; k <= 60; k += 15)` and `cfd::Index` is
`std::size_t` — **unsigned** (`Types.hpp:13`) — so `k = -60` wrapped to ~1.8e19, failed `k <= 60`
immediately, and the loop ran **zero** times. All four field dumps in `logs/10` printed a column
header and no rows. It failed in the safe direction, but an empty table under a header can read as
"no features present", so `logs/11` adds an explicit `*** FIELD DUMP EMPTY -- PROBE DEFECT` guard.
`logs/10` is annotated in place: its trajectory tables are valid, its field dumps are not.

---

# H. THE REPLACEMENT READING (`logs/13`) — **THE FRONT OSCILLATES. IT DOES NOT RUN AWAY.**

## H.1 Test A — the dead band is not implicated

```text
deadBand  offset cell   fuel bal     S_c       delta    S_L       march  recentr  rejects
   1.0      -46.55     3.652e-01   3.04348    0.7364   4.85901    3973     252      532
   3.0      -46.58     3.647e-01   3.04347    0.7365   4.85901    3973       7      470
  10.0      -45.68     3.642e-01   3.04549    0.7372   4.85901    3973       2      682
   6.0       +0.33     2.884e-02   4.89561    1.2276   4.80016    4072       2      460  <- see H.1.1
```

Three rows share `mdot = 4.85901` exactly, and across a **10x** range of the dead band the offset is
flat to 2 % and the balance is unchanged. **Band 1.0 performed 252 recentrings against 7 at the
default and it made no difference.** The dead band is not what permits the departure, and more
frequent recentring does not prevent it. **(d) and (e) are refuted.**

### H.1.1 The band-6.0 row is confounded, and is reported rather than used

Changing the dead band also changes recentring **during the trials**, so it changes the bracketing:
that row converged to `S_L = 4.80016` instead of `4.85901`. It is therefore not comparable with the
other three. It is still worth recording, because a **lower** `mdot` gave a **good** state, which
runs against G.3(b)'s "too-low `mdot` creeps upstream". With two things changed at once it cannot
settle the question either way.

## H.2 Test B — the state RECOVERS, which no runaway can do

```text
relaxT  march steps   offset cell    fuel bal     S_c cm/s   delta mm   rejects
  10       3968         +1.17       1.531e-02    4.90004     1.2289      472
  20       7937       -163.08       7.124e-01    1.77029     0.4081     2457
  30      11905       -162.24       6.022e-01    1.79459     0.4188     4441
  40      15874         +1.21       1.049e-02    4.88499     1.2232     6425
```

Good → catastrophically bad → bad → **good again, with the best fuel balance measured anywhere in
this item** (`1.049e-02`, better than the reference `1.531e-02`). The entire structure participates:
the thickness collapses `1.229 → 0.408 mm` and recovers to `1.223`, and `S_c` goes
`4.90 → 1.77 → 4.88`.

**An exponentially unstable front cannot come back. (c) is refuted.**

### H.2.1 The cross-check that makes this trustworthy

`relaxT 30` gives `fuel balance 6.022e-01` at 11905 march steps. TD-004's C2 measured
**`6.022411e-01` at 11905 steps**. Identical to every digit printed, from a different probe on a
different day — so the behaviour is deterministic and both measurements are of the same thing.

## H.3 What this explains, which is most of the item

```text
TD-004's C2            "the balance degrades with march length" -- it does NOT. TD-004 sampled
                       relaxT 10/30/90 and caught one good phase and two bad ones. C2's premise
                       of a MONOTONE TREND is wrong, so its "ratio <= 2.0" bounds a trend that
                       does not exist and was never satisfiable. See H.5.
C3's 5 of 12           those configurations stopped their march in a bad phase
sweep A  16 bad, 32 good, 64 bad          phase at stop, not trial length
sweep C  1.5 bad, 3.0 good, 6.0 bad       phase at stop, not relaxation
sweep D  all three good                   all three marched 3968-3976 steps -- the SAME phase.
                                          That is why no bracket width produced a bad mode, and
                                          it was NOT because the bracket is irrelevant.
plateau test           anchor 0.30 stopped in a bad phase; anchors 0.40 and 0.50 did not
logs/11's "exponential growth"            the rising limb of one half-cycle
```

**One mechanism, and it is simpler than any of the four readings before it:** the final march does
not reach a steady state. It executes a large-amplitude, long-period oscillation in front position
and flame structure, and **the reported state depends on the phase at which the march happens to
stop.**

## H.4 What is ESTABLISHED, and what is a lead

```text
ESTABLISHED
  * the march is NON-MONOTONIC in length and RECOVERS to a better state      logs/13 Test B
  * the recovery is not marginal: 1.049e-02 is the best balance in the item
  * the whole structure oscillates, not just the front position (delta, S_c)
  * it is deterministic -- it reproduces TD-004's C2 digit for digit
  * the dead band is not implicated over a 10x range at fixed mdot           logs/13 Test A
  * the solver already documents that `steadyStateReached` is UNREACHABLE by construction
    (PremixedFlame.hpp, F13). An oscillation is why: there is no steady state to reach.

A LEAD, NOT A FINDING
  * the PERIOD. The front is near the anchor at ~3968 and ~15874 steps, suggesting ~11900
    steps ~ 0.45 s. The header quotes the flame's structure relaxation time as 0.55 s for this
    fixture, so the period is the same order as the flame's OWN timescale -- which would make
    this intrinsic rather than a numerical artefact. FOUR POINTS CANNOT ESTABLISH A PERIOD.
  * whether it is a LIMIT CYCLE or a slowly decaying transient. Nothing here distinguishes them,
    and the distinction decides whether a longer march converges or never will.
```

## H.5 The consequence for TD-004's C2, stated plainly

C2 was frozen as *"the balance no longer degrades with march length: ratio <= 2.0"*. The premise is
false — there is no monotone degradation to bound. TD-004 **stopped at C2 and preserved the
failure**, which was the right action under its own protocol, and the measured ratio of `46.9362`
was real. But the criterion was measuring the phase difference between two arbitrary sampling
points, and **no fix to the recentring operator could ever have satisfied it.**

That is a mis-derived frozen criterion in the sense of CLAUDE.md §5.3, discovered after the freeze,
and the remedy there is explicit: preserve the original failure, investigate without changing
production, derive the replacement from first principles, and **request authorization**. It is not
TD-005's to amend.

## H.6 Falsification tests for §H, pre-registered

```text
* a FINE ladder in finalMarchRelaxationTimes (say 10 to 44 in steps of 2) must trace a smooth,
  roughly periodic curve in the front offset. IF the pattern is irregular or non-repeating, this
  is not a simple oscillation and §H is wrong.
* if it is a DECAYING transient rather than a limit cycle, the amplitude must shrink at relaxT
  60, 80, 120. IF the amplitude is undiminished, it is a limit cycle and no march length
  converges.
* the period should scale with the flame's own timescale alpha/S_L^2. Changing the pre-exponential
  by 10x moves S_L by ~3x (measured: 4.86 -> 15.36 cm/s), so the period should move by ~9x. IF it
  does not move with S_L, it is numerical rather than physical.
```

**Both directions are named for each test**, which is the practice this item adopted after §A.5
nominated only one direction and the hypothesis died by the other.

---

# I. THE PERIOD, MEASURED (`logs/14`) — A NEARLY SQUARE LIMIT CYCLE AT 14 RELAXATION TIMES

## I.1 Ten points, and NOT ONE of them is intermediate

```text
relaxT    10    14    18    22    26    30    34    38    42    46
front   c176  c178   c12  c176  c176   c13   c12  c177  c176   c10
offset    +1    +3  -163    +1    +1  -162  -163    +2    +1  -165
balance 2e-02 1e-02 7e-01 1e-02 1e-02 6e-01 7e-01 1e-02 1e-02 1e+00
```

The front is at the **anchor (cell ~176)** or at the **inlet (cell ~11)**. Nothing in between, in ten
samples. Combined with Test B's four points that is **fourteen measurements and no intermediate
state**, which a smoothly oscillating front could not produce.

## I.2 The period is 14 relaxation times, and it explains all twelve verdicts — **INVALID AS AUTHORITATIVE (TD-032)**

Bad windows centred at relaxT **18, 32, 46** — spacing 14 — with a half-width between 2 and 4:

```text
relaxT  10  14  18  20  22  26  30  34  38  40  42  46
measured G   G   B   B   G   G   B   B   G   G   G   B
predicted G   G   B   B   G   G   B   B   G   G   G   B      ALL 12 CONSISTENT

period = 14 relaxation times = 5558 march steps = 0.2095 s
one relaxation time          =  397 march steps = 0.0150 s
duty cycle in the bad state  = 4 to 8 of 14 = 29 % to 57 %
```

**The duty cycle predicts C3's failure rate.** C3 failed at **5 of 12** swept configurations = 42 %,
inside the 29–57 % band. The 5-of-12 was never about which knob was moved; it is the fraction of
march lengths that happen to stop in the bad half of the cycle.

## I.3 Scoring §H against its own pre-registered falsifier

H.6 said: *"IF the pattern is irregular or non-repeating, this is not a simple oscillation and
section H is wrong."*

```text
REPEATING?   YES -- period 14, all twelve measurements consistent.  H SURVIVES this test.
SMOOTH?      NO  -- the waveform is nearly SQUARE, a relaxation oscillation between two states.
                    H's word "oscillation" is right; any reading of it as sinusoidal is not.
```

**And one of my own numbers in H.4 was wrong.** H.4 offered a period of "~11900 steps ~ 0.45 s" as a
lead, from the four points at relaxT 10/20/30/40. The true period is **5558 steps / 0.2095 s** — the
earlier figure was **aliased by 2.1x** because a step of 10 in relaxT undersamples a period of 14.
H.4 labelled it "A LEAD, NOT A FINDING" and said four points cannot establish a period, which is
exactly why it was labelled that way; the label did its job.

## I.4 AMPLITUDE SUSTAINED — consistent with a LIMIT CYCLE, which is the costly outcome — **INVALID AS AUTHORITATIVE (TD-032)**

```text
peak |offset|, relaxT 10-26 (early half)   162.8 cells
peak |offset|, relaxT 30-46 (late half)    165.0 cells
```

No decay over a 4.6x range of march length. If this is a limit cycle then **no march length
converges**, and the solver's "march until the state stops changing" design is unsound rather than
merely slow. That also explains, exactly, why `finalMarchTolerance` was found to be unreachable by
construction (F13) and why `steadyStateReached` could only ever be false: **there is no steady state
to reach.**

**NOT YET ESTABLISHED:** limit cycle versus a very slow decay. Ten points over 10–46 cannot
distinguish an undecaying cycle from one with a decay time much longer than 46 relaxation times.
H.6's second test — relaxT 60, 80, 120 — is what separates them, and it is the measurement that
decides whether a march-length criterion can fix C3 at all.

## I.5 What this does to the three items

```text
TD-005 C3   the cause is now quantitative: the reported state is sampled from a limit cycle, and
            29-57 % of march lengths land in the bad half. C3 STILL FAILS; nothing is fixed.
TD-004 C2   confirmed mis-derived. Its ratio bounds a monotone trend across two sample points of
            a periodic signal. Not TD-005's to amend -- it needs its own authorization.
TD-004's    "flashback" reading is VINDICATED AS AN OBSERVATION: at the bad phase the front sits
logs/15     at cell 10-13, x ~ 0.20-0.26 mm, and TD-004 measured its worst rejected crossing
            landing at 0.1969 mm = cell 9. THE SAME PLACE. It was wrong only as a description of
            a steady state, because the state is not steady.
```

---

# J. IT IS A LIMIT CYCLE (`logs/15`) — AND `S_L` IS UNAFFECTED BY IT

## J.1 The measurement

Sampled **at** the period-14 model's predicted bad-window centres, cycles 4 through 8:

```text
relaxT  marchStp  frontCel  offset cell  front mm   fuel bal   S_c cm/s  delta mm  S_L cm/s
    60     23811        11     -163.99    0.2201   8.657e-01   1.75252    0.3970   4.86230
    74     29367        12     -163.35    0.2330   7.532e-01   1.76237    0.4037   4.86230
    88     34923        12     -162.84    0.2432   6.795e-01   1.77735    0.4107   4.86230
   102     40479        13     -162.45    0.2509   6.296e-01   1.78845    0.4161   4.86230
   116     46035        10     -165.04    0.1991   1.405e+00   1.79167    0.3586   4.86230

amplitude cycle 4 -> cycle 8:  163.99 -> 165.04 cells,  ratio 1.0064   UNDIMINISHED
rows in the good phase: 0 of 5
```

**Two results at once.**

1. **The period-14 model predicted all five, and all five are bad.** With `logs/14`'s ten and Test
   B's four, that is **seventeen measurements explained by one parameter**, out to 8 cycles.
2. **The amplitude is undiminished at 8 periods** over a march length range of `3968 → 46035`
   steps, an 11.6x span. This is a **LIMIT CYCLE**, not a decaying transient.

## J.2 The consequences, and the first is structural

```text
* NO MARCH LENGTH CONVERGES. "March until the state stops changing" is unsound as a design for
  this problem, not merely slow. C3 CANNOT be fixed by marching longer, and a march-length
  criterion -- the obvious remedy -- is ruled out by measurement.
* F13 IS EXPLAINED. `finalMarchTolerance` was found "unreachable by construction" and
  `steadyStateReached` could only ever be false. The reason is now known: THERE IS NO STEADY
  STATE TO REACH. What was recorded as a tolerance problem is a well-posedness problem.
* TD-004's C2 IS DEFINITIVELY MIS-DERIVED. It bounds the ratio of a monotone degradation with
  march length. The signal is periodic with constant amplitude, so there is no ratio to bound
  and no value of the threshold would have been correct.
```

## J.3 THE EIGENVALUE IS NOT AFFECTED, and this is the most important line in this file — **NARROWED by TD-032 §5**

```text
S_L = 4.86230 cm/s in ALL FIVE rows above.
S_L = 4.86230 cm/s in ALL TEN rows of logs/14.
S_L = 4.86230 cm/s in ALL FOUR rows of Test B.
```

Nineteen solves spanning march lengths from 3968 to 46035 steps, across every phase of the cycle,
and **the flame speed is identical to six figures in every one.** The oscillation moves the front
by 165 cells, collapses the thickness by 3x and swings the fuel balance over two orders of
magnitude — and does not move `S_L` at all.

That is the bracketing design working exactly as `PremixedFlame.hpp:89-99` claims: the eigenvalue is
decided by **sign(drift)** in the trials, and a sign is insensitive to the phase the march happens
to end in. So:

```text
SOUND       the eigenvalue extraction -- which is TD-005's actual subject. S_L is reproducible
            to 6 figures across 19 solves and every phase of the limit cycle.
UNSOUND     the claim that the REPORTED STATE is a steady flame. It never is. Therefore the
            conservation checks that exist to VALIDATE the eigenvalue -- the fuel balance and
            the consumption identity S_c == mdot/rho_u -- cannot be satisfied, because they
            measure a state that is not steady.
```

**The honest position for G8:** a flame speed from this solver is reproducible and defensible; the
*validation* of it is not, because the identity that would validate it requires a steady state the
solver does not produce. That is a sharper and smaller problem than "the flame speed is unreliable",
and it is the opposite of what every earlier reading in this file implied.

## J.4 IS THE OSCILLATION PHYSICAL? A derived argument that it is probably NOT — **CONFIRMED by TD-032**

```text
Ze = E_a (T_ad - T_u) / (R_u T_ad^2) = 1.2e5 * 1500 / (8.314 * 1800^2) = 6.682
Le = 1.0 exactly, by construction (rho D_i = lambda / cp, unity Lewis for every species)
Ze * (Le - 1) = 0.000
```

The classical pulsating instability of a 1D planar premixed flame requires `Ze(Le-1)` above a
positive threshold of order ten. **At `Le = 1` the product is exactly zero**, so this configuration
sits far below any pulsating threshold and a sustained oscillation is **not** physically expected.
Real pulsating flames need `Le` appreciably above unity at this `Ze`.

So the limit cycle is **probably numerical**, and the next investigation belongs in the numerics —
most plausibly the moving-frame treatment or the time integration, not the chemistry or the
transport. **This is an argument, not a measurement**, and it is labelled as such.

### J.4.1 How to test it, pre-registered

```text
* vary Le. If the oscillation is physical its amplitude must depend strongly on Ze(Le-1) and
  should VANISH as Le -> 1 from above. It is already AT Le = 1 and oscillating, so any
  dependence at all is informative; NO dependence on Le points hard at the numerics.
* vary the mesh. A numerical instability's period or amplitude generally scales with dx or dt;
  a physical one scales with the flame's own scales. logs/01 sweep D already showed the period
  is insensitive to the BRACKET; the mesh has not been varied for the PERIOD.
* disable recentring entirely over a short march. If the oscillation needs the moving frame, it
  should disappear -- though the front will then leave the domain, which bounds the test length.
```

**None of this is authorized and none of it is a fix.** It is where a fix would have to start.

---

# K. THE LIMIT CYCLE IS NUMERICAL, AND IT IS NOT THE RECENTRING (`logs/16`)

## K.1 My probe's N1 verdict is WRONG, and the reason is worth more than the verdict

`logs/16` prints `=> PATTERN CHANGED with recentring off. The moving frame IS implicated.` **That
conclusion is incorrect.** The probe classified each row as good or bad by `|offset| > 1.0 mm` — and
**N1 disables the very mechanism that holds the front at the anchor**, so offset-from-anchor stops
being a health measure the moment the test intervenes.

```text
relaxT   offset mm    fuel bal     delta    by OFFSET   by HEALTH
    10      0.4403   1.130e-02    1.2292      good        good
    18     -3.2157   4.370e-01    0.4322      BAD         BAD
    26      3.0103   6.014e-03    1.2254      BAD         good    <- misclassified
    32     -3.2177   4.458e-01    0.4314      BAD         BAD
    40      5.2801   5.789e-03    1.2261      BAD         good    <- misclassified
    46     -3.2177   4.459e-01    0.4314      BAD         BAD

pattern by OFFSET  GBBBBB      <- the probe's verdict rests on this
pattern by HEALTH  GBGBGB      <- IDENTICAL to the 500-cell baseline
```

Rows 26 and 40 sit **150 and 264 cells downstream** and have the **best fuel balances measured
anywhere in this item** (`6.0e-03`, `5.8e-03`) with a healthy `delta ~ 1.226`. They are not bad
states; they are good states that drifted downstream because nothing was holding them. Recentrings
were **0**, so the intervention worked — the observable did not.

**CORRECTED N1 VERDICT: the cycle persists unchanged, in the same phase, with recentring entirely
disabled. THE MOVING FRAME IS NOT THE MECHANISM.** Together with Test A (flat over a 10x dead-band
range) the recentring operator is now cleared twice, by two independent interventions.

**The lesson, which generalises:** an automated verdict is only as good as its observable, and an
intervention that removes a control can invalidate the observable the verdict uses. The fix is to
judge health by a quantity the intervention does not touch — here the fuel balance and the flame
thickness, both of which are properties of the profile rather than of its position.

## K.2 Test N2 — the period MOVES with the mesh — **INVALID AS AUTHORITATIVE (TD-032: parity, not a period)**

```text
relaxT   offset mm   offset cel    fuel bal   S_L cm/s   delta mm   phase
    10     -1.4373      -35.93   5.255e-01    4.77424     0.5367    BAD
    18      0.0308       +0.77   1.173e-01    4.77424     0.9930    good
    26     -3.2266      -80.66   4.858e-01    4.77424     0.3739    BAD
    32     -3.2218      -80.54   4.596e-01    4.77424     0.3723    BAD
    40      0.0640       +1.60   1.177e-01    4.77424     0.9965    good
    46     -3.2312      -80.78   5.125e-01    4.77424     0.3760    BAD

pattern  BGBBGB      against the 500-cell baseline  GBGBGB
```

```text
period, 500 cells (dx 20 um):  bad centres 18, 32, 46      spacing 14 relaxation times
period, 250 cells (dx 40 um):  bad centres ~10, ~29, ~46    spacing ~18     (+29 %)
```

**`finalMarchRelaxationTimes` is a PHYSICAL unit** — a multiple of `alpha/S^2`. A period that is
29 % different when expressed in the flame's own time unit, after doubling `dx`, is **set by the
discretisation**. `S_L` shifts only 1.8 % between the meshes (`4.86230 -> 4.77424`, the ordinary
mesh-convergence error), nowhere near enough to account for it.

**THE LIMIT CYCLE IS NUMERICAL.**

### K.2.1 The amplitude's mesh-independence is GEOMETRIC, not evidence of physics

The probe flagged `3.231 mm` against `3.300 mm` as a near-invariant amplitude, which would normally
point AWAY from a numerical origin. It does not here: `-3.23 mm` from a `3.5 mm` anchor puts the
front at `0.27 mm`, i.e. **at the inlet**. Both meshes share the same 10 mm domain and 3.5 mm
anchor, so the excursion is bounded by the **geometry** and must come out the same. Its invariance
carries no information about the mechanism, and reading it as physical would have been a mistake the
probe's own verdict text invited.

## K.3 Where this leaves the diagnosis

```text
CLEARED (twice, by independent interventions)
  the recentring operator and the moving frame     Test A (10x dead band), Test N1 (disabled)
CONFIRMED
  a NUMERICAL origin -- the period scales with dx  Test N2
  J.4's Ze(Le-1) = 0 argument, now corroborated by measurement rather than standing alone
STILL UNLOCATED
  which part of the numerics. The remaining candidates are the implicit-Euler march itself, the
  convective/diffusive discretisation of the species and energy equations in the march, and the
  operator split between chemistry and transport. NONE of these has been tested.
```

**No further hypothesis is offered about which.** The pattern of this item is that each narrowing
came from an intervention that removed one candidate, and the next step is the same kind: a march at
a fixed, deliberately small `dt` (decoupling `dt` from `dx`) would separate the TIME integration
from the SPATIAL discretisation, because only the former changes. `timeStep` exists as an explicit
override for exactly this, so it needs no production change.

## K.4 What does NOT change

`S_L = 4.86230 cm/s` at 500 cells in every one of the 25 solves now recorded, and `4.77424` at 250
cells in all six. The eigenvalue remains untouched by any of this: the numerical limit cycle moves
the front across 165 cells and swings the fuel balance over two decades without perturbing the
sixth figure of the flame speed. **J.3's split stands: the extraction is sound and its validation is
not.**

---

# L. `S_L` IS NOT CONVERGED IN TIME, AND THE ERROR EXCEEDS G8's BAND (`logs/17`)

**This section corrects §J.3, which is the most consequential claim I have made in this item.**

## L.1 The period question, answered: neither knob owns it

```text
cells   dt           cell Courant   cell diffusion   pattern   period (relaxation times)
  500   3.770e-05       0.092            3.33        GBGBGB         14
  500   1.885e-05       0.046            1.67        GBBGBG         ~18
  250   3.770e-05       0.046            0.83        BGBBGG         ~19
  250   ~1.45e-04       0.176            3.20        BGBBGB         ~18
```

T1 (dx doubled, dt pinned) gave **neither** reference pattern, and T2 (dx fixed, dt halved) **moved**
the pattern. The probe's third branch fired: **the period is set by a combination of `dt` and `dx`,
not by either alone.** Two rows share a cell Courant number of `0.046` and both give ~18–19, which
is suggestive — but the fourth row has `Co = 0.176` and also gives ~18, so `Co` alone does not
explain it either. **Four points, no fit offered.**

What is unambiguous: **the period does not converge under refinement of either knob.** Halving `dt`
moved it `14 → 18`. A numerical artefact that moves rather than diminishes under refinement is not
converging away.

## L.2 THE RESULT THAT MATTERS: `S_L` MOVES WITH `dt`

```text
500 cells, derived dt 3.770e-05   S_L = 4.86230 cm/s
500 cells, dt HALVED  1.885e-05   S_L = 4.65615 cm/s     -4.24 %
250 cells, dt pinned  3.770e-05   S_L = 4.82296 cm/s     -0.81 % (spatial, at fixed dt)
```

```text
TEMPORAL error, dt -> dt/2 at fixed dx :  -4.24 %
SPATIAL  error, dx -> 2dx at fixed dt  :  -0.81 %
G8's band, 35.0-37.0 cm/s              :  +- 2.80 %

THE TIME-DISCRETISATION ERROR IN S_L IS 1.5x G8's ENTIRE BAND.
```

### L.2.1 This corrects §J.3

§J.3 said *"the eigenvalue extraction is SOUND — `S_L` is reproducible to six figures across 19
solves and every phase of the limit cycle"*. **That is true and it is not the same claim as
convergence.** All 19 of those solves ran at the **same `dt`**. `S_L` is insensitive to march
length and to the phase of the limit cycle; it is **not** insensitive to the time step. I read a
phase-invariance result as a convergence result, and they are different properties.

The corrected split:

```text
STILL TRUE   S_L is invariant to march length and limit-cycle phase (19 solves, 6 figures)
FALSE        "the eigenvalue extraction is sound". It is not converged in time, by 4.24 %,
             which is larger than the band G8 would judge it against.
```

## L.3 Why the suite's mesh-refinement test cannot see this

`TheFlameSpeedConvergesUnderMeshRefinement` **passes** (301 s, `logs/03`), over `{125, 250, 500}`
cells. It refines **cells**, and `dt` is DERIVED from `dx`
(`dt = min(Co*dx/u_in, D*dx^2/alpha)`), so refining the mesh refines `dt` at the same time. And the
two errors have **opposite signs**:

```text
refining dx  RAISES S_L     (+0.81 % from 250 to 500 cells at fixed dt)
refining dt  LOWERS S_L     (-4.24 % from dt to dt/2 at fixed dx)
```

So under combined refinement they partially cancel, and the test reports convergence while
**neither component is converged**. This is exactly the failure CLAUDE.md §6 warns about when it
requires the mesh family to be *self-similar*: here the family varies two parameters at once and
the observed order is meaningless.

**The test is not wrong about what it measures; it is measuring the wrong family.** A valid
convergence study for this solver needs `dt` and `dx` refined **independently**, which no test,
probe or gate in this repository has ever done.

## L.4 What this does to the items

```text
TD-005   section 3.1's precision precondition -- "method reproducibility << 2.8 %" -- is NOT met,
         and the dominant term is neither the drift window (0.068 %) nor relaxationTimesPerTrial
         (2.19 %, and itself probably contaminated) but the TIME STEP, at 4.24 %. All three of
         this item's earlier precision readings were measured at a single dt.
TD-006   G8 cannot close on this solver as it stands. A flame speed whose time-discretisation
         error is 1.5x the acceptance band cannot be judged against that band, which is exactly
         the precondition flame_solver_findings.md 3.1 set out and which still is not satisfied.
TD-004   unaffected -- C2 remains mis-derived for the reasons in section H.5.
NEW      the mesh-refinement test's family is not self-similar. This is a defect in a PASSING
         test and belongs in the register.
```

## L.5 What is NOT established

```text
* the ORDER of the temporal error. Two dt values give a difference, not an order; three or more
  at fixed dx would give one, and would say whether implicit Euler is behaving at first order.
* whether a converged dt exists at acceptable cost. dt/2 cost ~2x and did not converge the
  answer -- it MOVED it -- so dt/4 and dt/8 are needed to see the trend, at 4x and 8x cost.
* whether the limit cycle and the dt error are the same defect or two. They move together here,
  but nothing separates them yet.
```

**No fix is proposed and none is authorized.** The next measurement is a dt ladder at fixed `dx` --
`{dt, dt/2, dt/4, dt/8}` -- which would give the temporal order and show whether `S_L` converges at
all. That is the measurement G8 has needed since `flame_solver_findings.md` §3.1 was written.

---

# M. `S_L` DOES CONVERGE IN `dt` — AND THE DEFAULT `dt` IS +3.99 % OUTSIDE IT (`logs/18`)

**This refines §L and refutes my own pre-registered prediction.**

## M.1 The ladder

```text
  dt s      divisor   S_L cm/s   bracket      total steps   wall s
3.7697e-05   dt/1      4.86230   6.7713e-04      15692        212
1.8849e-05   dt/2      4.65615   6.7713e-04      15628        192
9.4243e-06   dt/4      4.67194   6.7713e-04      28530        365
4.7122e-06   dt/8      4.67510   6.7713e-04      64448       1108

differences:   dt/1 -> dt/2   -0.20615     large, NEGATIVE
               dt/2 -> dt/4   +0.01579     small, positive
               dt/4 -> dt/8   +0.00316     small, positive,  ratio 5.00
```

The probe's sign-change guard fired and **refused to print an observed order**, which was the
correct behaviour: CLAUDE.md §6 says an order is undefined across a zero crossing. But the shape is
unambiguous — **`dt/1` is an outlier outside the asymptotic range**, and `{dt/2, dt/4, dt/8}` is a
clean monotone family.

## M.2 On the asymptotic subset

```text
ratio of successive differences   4.997
observed order  p = log2(ratio)   2.321
Richardson limit (dt -> 0)        S_L = 4.67589 cm/s
GCI on the finest pair            0.0212 %
```

```text
error of each dt against the converged value:
   dt/1   4.86230   +3.987 %    OUTSIDE G8's +-2.80 % band
   dt/2   4.65615   -0.422 %    inside
   dt/4   4.67194   -0.084 %    inside
   dt/8   4.67510   -0.017 %    inside
```

**`S_L` converges, and fast.** The problem is not that convergence is unreachable — it is that **the
DEFAULT `dt` is not in the asymptotic range and is 4 % high**, while `dt/4` is within 0.08 %. This is
a far more tractable result than §L's framing, which rested on two points.

**Caveat on `p`:** three points give **one** ratio, so `p = 2.32` rests on a single estimate. It is
also higher than the `p ~ 1` expected of implicit Euler — but `S_L` here is a **bracketed eigenvalue
decided by drift SIGNS**, not a directly integrated quantity, so its error need not inherit the
integrator's order. A fourth asymptotic point would test it. **Reported, not relied upon.**

## M.3 My pre-registered prediction was WRONG, and the reason generalises

```text
PREDICTED   p ~ 1, S(dt/4) ~ 4.553, Richardson limit ~ 4.45, total correction ~ -8.5 %
ACTUAL      S(dt/4) = 4.67194, limit = 4.67589, correction from the default +3.99 % (opposite sign)
```

The prediction extrapolated a first-order trend from the **single** `dt/1 -> dt/2` interval — the one
interval that is **not** asymptotic. That is precisely why CLAUDE.md §6 demands three or more levels
and an asymptotic-range check before an order is claimed: a two-point "order" is a slope through an
outlier. I had the rule in front of me and still predicted from two points.

## M.4 The root cause, and it is a settable constant

> **CORRECTED 2026-10-05 by TD-027's audit.** The `D` values below use the **unburnt** diffusivity.
> The code uses the **burnt** one (`PremixedFlame.cpp:481`, *"alpha_max is taken at the BURNT side"*),
> which is **6x larger**: `alpha_burnt = 2.1221e-04` against `alpha_unburnt = 3.5368e-05 m2/s`. In the
> code's own convention:
>
> ```text
>    dt/1  D = 20.00   +3.99 %      <- the cap is SATURATED, not loosely respected
>    dt/2  D = 10.00   -0.42 %
>    dt/4  D =  5.00   -0.08 %
>    dt/8  D =  2.50   -0.02 %
>
> 20 * dx^2 / alpha_burnt = 3.769930e-05 s  against the measured dt 3.76972e-05 s   MATCHES
> ```
>
> So **`dt` sits EXACTLY at the cap and `D` is identically `maximumCellDiffusionNumber`** — the
> sentence below about the cap "permitting six times the realized value" is wrong, and the real
> situation is simpler and worse. A further consequence the original text missed: since
> `dt = cap * dx^2/alpha`, **`D` is independent of `dx`**, so refining the mesh cannot improve it at
> all. See `TD027_cell_diffusion_cap/audit.md` §1 and §3.

Accuracy evidently needs `D <~ 1`, the default `dt` realizes `D = 3.33`, and **the cap permits 20** —
six times the realized value and about twenty times what accuracy requires. The header states these
limits are *"resolution limits"* rather than stability limits, which is correct for implicit Euler,
**but the value 20.0 was never validated against a measured error.** That is the defect: a resolution
cap set by assertion.

**This also explains §K/§L's period behaviour without a new hypothesis.** The limit cycle's period
moved between configurations whose `D` ranged `0.83` to `3.33` — i.e. across the boundary of the
accurate region. Whether the cycle survives at `D < 1` is **not measured here**: `logs/18` used a
1.0-relaxation-time march precisely because it was measuring `S_L`, not the cycle. That is the next
measurement, and it is cheap at `dt/4`.

## M.5 Where the items stand now

```text
TD-005  section 3.1's precision precondition is MET at dt/4 and below (0.08 % against << 2.80 %),
        and FAILS at the default dt (+3.99 %). The precondition was never a property of the
        method -- it is a property of the time step, and the default is the wrong one.
TD-006  G8 can be decided once the default dt is corrected or overridden. Every S_L figure in
        this repository was computed at D = 3.33 and is therefore about 4 % HIGH, including the
        4.86230 quoted throughout TD-004 and TD-005. The converged synthetic value is 4.67589.
NEW     maximumCellDiffusionNumber = 20.0 is an unvalidated resolution cap that admits a 4 %
        error in the gated quantity. Register item.
NEW     the mesh-refinement test's family is not self-similar (section L.3). Register item.
```

**No production change is made.** Correcting a default that moves every recorded `S_L` by 4 % is not
within "fix the report", and it would invalidate figures in two items' frozen evidence. It needs its
own authorization, its own gate, and a decision about what to do with the existing numbers.

---

# N. AT `dt/4` THE MARCH CONVERGES — AND A SECOND, SEPARATE DEFECT IS LEFT BEHIND (`logs/19`)

> **ATTRIBUTION CORRECTED 2026-10-06.** This section concludes that the cycle is governed by the
> cell diffusion number `D`. **That is refuted.** TD-031 `logs/01` measured the cycle at **`D = 5`
> with 250 cells** (`dt = 3.77e-05`) — the same `D` at which it is suppressed with 500 cells — and
> at `cap 10 / 500 cells`. The behaviour follows **absolute `dt`**, not `D`:
>
> ```text
> dt 9.4246e-06  no cycle        dt 1.8849e-05  cycle        dt 3.7699e-05  cycle
> and dt 3.7699e-05 produces a cycle at BOTH (cap 5, 250 cells) and (cap 20, 500 cells) --
> same dt, different D, same behaviour.
> ```
>
> **What this section still establishes correctly:** that the march DOES converge at
> 500 cells / `dt/4`, which TD-027's C4 independently confirmed, and that a second defect
> (the `8.48e-02` steady residual, now TD-030) is left behind when it does. Only the
> `D`-governance reading is wrong. Full record:
> `TD027_cell_diffusion_cap/attribution_correction.md`.

## N.1 The measurement

```text
relaxT  marchStp  offset cel   fuel bal   delta mm   S_L cm/s   phase
    10     17194     -34.25   8.602e-02    0.9909    4.67194    good
    14     24072    -124.70   4.395e-01    0.5505    4.67194    BAD
    18     30949       1.46   8.477e-02    1.2781    4.67194    good
    22     37827       2.01   8.464e-02    1.2779    4.67194    good
    26     44705       2.56   8.454e-02    1.2783    4.67194    good
    30     51583       0.11   8.636e-02    1.2778    4.67194    good
```

## N.2 The probe's own verdict is too crude, and the tail is the better discriminator

`logs/19` prints `=> PARTIAL`, from a **bad-row count** (1 of 6 against the baseline's 2 of 6). That
heuristic counts the row-14 excursion as a cycle phase. The tail says otherwise:

```text
rows 18, 22, 26, 30 -- a 1.67x range of march length, 30 949 to 51 583 steps:
   fuel balance   8.454e-02 .. 8.636e-02     spread 2.15 %
   delta mm          1.2778 .. 1.2783        spread 0.04 %
   |offset|            0.11 .. 2.56 cells    all inside the 3-cell dead band
```

**Four consecutive march lengths agree to 2 %.** On the same abscissa at the default `dt` the fuel
balance varied by a factor of **40** (`1.5e-02` to `6.8e-01`). A limit cycle cannot produce a flat
tail; **this is convergence.**

Rows 10 and 14 are a **startup transient the march passes through** — offsets `-34` and `-125`,
`delta` `0.99` and `0.55` — and the later rows do not return to those values, which is what
distinguishes a transient from a phase.

**CORRECTED VERDICT: the limit cycle is SUPPRESSED at `D = 0.833`.** §M.4's suggested connection
between the cell diffusion number and the cycle is **confirmed**.

**The caveat, stated rather than buried:** six points cannot exclude a cycle whose period exceeds
~20 relaxation times. Four consecutive steady rows is strong evidence, not proof, and a longer
ladder at `dt/4` would settle it.

### N.2.1 Second probe verdict in this item that needed correcting

`logs/16`'s N1 verdict was wrong because it classified by front position in a test that removed what
holds the front there. This one is wrong because it classified by a **count** when the informative
signal is the **flatness of the tail**. Both were pattern-matching heuristics applied to a sweep
whose character the intervention had changed. The lesson is narrow and practical: an auto-verdict
that reduces a sweep to a label is fragile precisely when the intervention works, and the raw rows
have to be read.

## N.3 TWO DEFECTS WERE CONFLATED, AND THEY ARE NOW SEPARATED

```text
DEFECT 1  the limit cycle, the phase dependence, the 29-57 % duty cycle, TD-004's C2 "degradation
          with march length", the unreachable finalMarchTolerance, and the +3.99 % S_L error.
          CAUSE: maximumCellDiffusionNumber = 20.0 admitting D = 3.33 at the default dt.
          SUPPRESSED at D = 0.833.

DEFECT 2  a STEADY-STATE fuel conservation error of 8.48e-02 in the converged state.
          STILL PRESENT at dt/4, and it fails C3 (< 0.05) by 1.7x.
          This is a real conservation defect in the discretisation, and it is NOT the cycle.
```

### N.3.1 The apparent paradox, resolved

The default `dt`'s *good-phase* balance was `1.53e-02`, **smaller** than the converged `8.48e-02`.
A more accurate time step giving a larger conservation residual looks contradictory, and is not: the
`1.53e-02` was a **mid-cycle sample of an unsteady state**, not a steady value. The `8.48e-02` is
the **true steady discretisation error**, visible for the first time because the state now actually
reaches steadiness. The cycle was masking it — the same shape as TD-004's finding that its 164-cell
shifts had been masking what C2 then exposed.

## N.4 Where every open thread now stands

```text
TD-005 C3   STILL FAILS, now for a single isolated reason: an 8.48e-02 steady fuel conservation
            error. The cycle, the phase dependence and the sweep non-monotonicities are
            explained and suppressible. C3's threshold is unchanged and unmet.
TD-005 C6   unchanged -- the TD-003 bound bundled into the consumption-identity test.
TD-004 C2   mis-derived, and now fully explained: it bounded a monotone trend in what is a
            limit cycle caused by an unvalidated resolution cap. Needs its own authorization.
TD-006      G8 needs the converged S_L (4.67589 cm/s synthetic, not 4.86230) AND defect 2
            resolved, because the consumption identity cannot validate a state with an 8.5 %
            conservation error.
REGISTER    two new items: maximumCellDiffusionNumber = 20.0 unvalidated (section M.4), and the
            mesh-refinement test's non-self-similar family (section L.3). Plus TD-026 from the
            C4d work.
```

**The next measurement, if authorized:** defect 2 is a steady conservation error, so it is
attributable by the same method TD-004 used for its ledger — integrate the discrete fuel equation
term by term on the converged `dt/4` state and see which term fails to close. The boundary flux is
the first candidate, since a Dirichlet wall diffusive flux is first order and the inlet carries the
full `Y_F,u`. **That is a hypothesis about where to look, not a diagnosis.**

---

# O. THE TERM THAT FAILS TO CLOSE IS THE **REACTION INTEGRAL** (`logs/20`)

## O.1 The probe validated itself before anything else

```text
recomputed residual  8.464453e-02      reported  8.464453e-02      agree: YES
```

Exact, at both march lengths. So the definition read out of `PremixedFlame.cpp:1630-1653` is the one
the solver uses, and the rest of the table can be trusted.

## O.2 The budget, on the converged `dt/4` state

```text
term                             kg/(m^2 s)   % of fuelThrough
fuelThrough (normaliser)        2.751859e-03     100.000 %
convection                     -2.751859e-03    -100.000 %    <- EXACTLY -100 %
reaction                       -2.969035e-03    -107.892 %    <- THE OVERSIZED TERM
inlet diffusion                -1.575318e-05      -0.572 %
outlet diffusion (OMITTED)     -1.907082e-14      -0.000 %    <- the omission is harmless
accumulation (OMITTED)          9.626236e-06      +0.350 %    <- real, and small
gap                             2.329298e-04      +8.464 %
UNEXPLAINED REMAINDER           2.233036e-04      +8.115 %
```

**Convection is exactly `-100.000 %` by construction** — `Y_F[last] = -1.2e-13`, so
`convected = -mdot * Y_F,u = -fuelThrough` identically. It carries **no** error and cannot be the
defect.

**Both omitted terms are accounted for and neither matters.** The outlet diffusive flux is `1e-14` —
the balance's omission of it is harmless, and the zero-gradient assumption holds. The accumulation
term is `+0.350 %`, so the state really is nearly steady, confirming §N's convergence reading
independently.

**The reaction integral is `-107.892 %` and is the sole oversized term.** It consumes 7.9 % more fuel
than convection delivers, which no steady state can do.

## O.3 It is the same statement as the `S_c` / `S_L` mismatch

```text
S_c / S_L              = 5.04065 / 4.67194 = 1.07892
reaction / fuelThrough =                      1.07892      IDENTICAL
```

So "the reaction term is 7.892 % oversized" and "the consumption speed exceeds the eigenvalue by
7.892 %" are one measurement wearing two faces — the same trap §4.3 of the gate rejected a criterion
for, and here it is useful rather than misleading: it means the budget pins the discrepancy to a
single quantity.

## O.4 THE TREND IS THE WRONG WAY, AND THAT IS THE SHARPEST RESULT

```text
                S_L        S_c       mismatch
default dt    4.86230    4.90004     +0.776 %
dt/4          4.67194    5.04065     +7.892 %
```

**Refining `dt` made the two independent measures of the flame speed DISAGREE MORE** — by a factor
of ten. The identity `S_c == mdot/rho_u` is exact for a converged, conservative discretisation, so
two things follow:

```text
* at the DEFAULT dt the two agreed to 0.78 % by CANCELLATION, not by correctness. That agreement
  was being read as a consistency check passing -- `TheConsumptionIntegralAgreesWithTheEigenvalue`
  asserts mismatch < 0.01 and PASSES there. It passes because two errors of opposite sign cancel,
  exactly as the mesh-refinement test does (section L.3). THAT IS THE SECOND PASSING TEST IN THIS
  SOLVER FOUND TO PASS BY CANCELLATION.
* the "converged" S_L = 4.67589 from logs/18 is NOT corroborated. The consumption integral, an
  independent route to the same quantity, says 5.04. One of them is wrong by ~8 %, and the dt
  ladder alone cannot say which, because both were computed from the same march.
```

**G8 consequence:** the two independent measures of the synthetic flame speed differ by **7.9 %**
against a band of **+-2.8 %**. G8 cannot be decided until they agree, and this is a sharper statement
of the precondition `flame_solver_findings.md` §3.1 set out than anything measured before.

## O.5 A CANDIDATE MECHANISM, labelled as such

The march integrates chemistry in an operator split with a sub-step limiter,
`chemistryFractionalChange = 0.1` — *"a sub-step may consume at most this fraction of any species'
local mass"*. The reported `fuelIntegral`, by contrast, is **recomputed from the final fields** by
`arrheniusProgressRatesFromFields` with **no limiter**. If the limiter is active in the front during
the march, the march's *effective* consumption is lower than the recomputed rate — which would make
the reported reaction integral an overestimate, with the gap growing as `dt` falls because a smaller
`dt` puts more sub-steps against the same limiter.

**This is a hypothesis and nothing here tests it.** What would:

```text
* sweep chemistryFractionalChange {0.1, 0.03, 0.01}. If the limiter is responsible, the mismatch
  must depend on it; if the mismatch is unchanged, the limiter is cleared.
* report the limiter's own activity. ChemistryIntegrator tracks sub-step counts; a front in which
  the limiter never binds would clear it immediately and cheaply.
* integrate omega over the march as APPLIED rather than recomputing it at the end. That is the
  direct measurement, and it needs a production change.
```

Given this item's record — four readings refuted before the fifth held — the limiter is where I
would look and not what I would report as the cause.

## O.6 Status

```text
ANSWERED   the question "which term fails to close": the REACTION INTEGRAL, by 7.892 %.
           Convection is exact, inlet diffusion is 0.6 %, the two omitted terms are 0.35 % and
           1e-14, and the accumulation term independently confirms the state is steady.
NEW        TheConsumptionIntegralAgreesWithTheEigenvalue passes at the default dt by CANCELLATION
           and fails by 7.9 % once dt is converged. Second passing test found to pass by
           cancellation. Register item.
UNCHANGED  C3 still FAILS. C6 unchanged. No production file touched. No threshold moved.
```

---

# P. THE LIMITER HYPOTHESIS IS REFUTED — AND `S_c` IS NOT A USABLE INSTRUMENT HERE (`logs/21`)

## P.1 The sweep

```text
fraction   S_L cm/s   S_c cm/s   mismatch    fuel bal    chemSubSteps   perStep
   0.100    4.67194    5.04065   + 7.892 %   8.464e-02      32 319 000    500.00
   0.030    4.66562    2.06929   -55.648 %   5.564e-01      74 216 604   1 223.65
   0.010    4.66878    1.88758   -59.570 %   6.468e-01     212 560 735   3 282.69
```

## P.2 My hypothesis is refuted, and not in the direction I allowed for

§O.5 predicted that tightening the limiter would **shrink** the mismatch. It did the opposite and
changed its **sign**: `+7.892 % -> -59.570 %`. The probe's third branch fired.

But the refutation is stronger than the sweep, and it is in the **first row**: the limiter binds at
**~500 sub-steps per march step at the DEFAULT settings.** My pre-registered cheap refutation was
"~1 per step means the limiter never binds". The truth is the far end of that scale — it binds
constantly and hard. So `chemistryFractionalChange` is not a lightly-engaged safety limiter; it is
doing 500x the work of the transport step on every step, and changing it changes the march wholesale
rather than refining one detail.

## P.3 `S_L` is insensitive; `S_c` collapses

```text
S_L   4.66562 .. 4.67194     spread   0.135 %     INSENSITIVE to the limiter
S_c   1.88758 .. 5.04065     spread 167.0   %     COLLAPSES
```

**The consumption integral swings by 167 % under a knob that moves the eigenvalue by 0.135 %.**

`PremixedFlame.hpp:113-117` introduces `S_c` as *"a sharp CONSISTENCY check on the discrete
solution"*. On this evidence it is not: in the converged-`dt` region it is wildly sensitive to a
numerical setting that barely touches the quantity it is supposed to be checking. **As an instrument
it is unusable here**, and that undermines its role in C3's admissibility reading, in C6, and in
whatever validation G8 would rest on.

**Conversely, this is the strongest evidence yet for the eigenvalue.** `S_L` has now survived:
19 solves across every phase of the limit cycle (6 figures identical), the recentring operator being
disabled entirely, a 10x dead-band range, a 100x bracket range, and now a 10x chemistry-limiter
range at 0.135 %. Its one established sensitivity is to `dt` (4.24 %, §M).

## P.4 What the 500 sub-steps per step mean

```text
dt          = 9.424e-06 s        chosen from TRANSPORT limits only:
dt / 500    = 1.885e-08 s            min(Courant * dx/u_in, D * dx^2/alpha)
                                 NOTHING in that derivation references the CHEMICAL timescale.
```

So a **first-order Lie operator split** is being applied at a `dt` roughly **500x the chemical
time**. The splitting error of a first-order split scales with `dt` times the commutator of the two
operators, and a 500:1 timescale ratio is exactly the regime where that term is large.

**That is now the leading candidate for the 7.892 %, and it is where §O's verdict already pointed**
("at the OPERATOR SPLIT rather than at either half of it"). It also explains why the mismatch grew
when `dt` was refined from the default to `dt/4`: the split error and the transport error move
differently, and the cancellation that held them to 0.78 % at the default `dt` broke.

**It remains a candidate.** What would test it:

```text
* STRANG splitting instead of Lie would make the split error second order. If the mismatch falls
  by the square, the split is responsible. This needs a production change.
* a dt chosen from the CHEMICAL timescale rather than the transport one -- dt small enough that
  sub-steps per step approaches 1. From the scaling above that is dt ~ 2e-08 s, which is ~470x
  smaller than the current dt and therefore ~470x the cost. Not affordable as a sweep, but a
  single solve at dt/50 would show whether the mismatch is falling.
* the fuel budget of section O recomputed with omega integrated AS APPLIED during the march
  rather than recomputed at the end. That is the direct measurement and needs a production change.
```

## P.5 Status

```text
REFUTED    the chemistry sub-step limiter as the cause of the reaction excess. Tightening it
           makes the mismatch worse and flips its sign, and the limiter was never idle.
NEW        the limiter binds at ~500 sub-steps per march step at the DEFAULT settings, so the
           chemistry is stiff relative to a dt chosen purely from transport limits. Register item.
NEW        S_c is not a usable consistency instrument in this regime: 167 % swing under a knob
           that moves S_L by 0.135 %. This weakens C6 and any G8 validation resting on it.
           Register item.
STANDING   S_L is robust to everything tested except dt. Five independent interventions.
UNCHANGED  C3 still FAILS. No production file touched. No threshold moved.
```
