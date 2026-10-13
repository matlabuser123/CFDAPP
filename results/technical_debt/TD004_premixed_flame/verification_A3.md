# TD-004 A3 — ALL THREE failing C5 clauses fail on the BASELINE too. None is a regression.

```text
STATUS:      INVESTIGATION under CLAUDE.md §5.3 steps 2-3. NO threshold changed. NO production
             change. NO amendment frozen. C5 remains FAILED as recorded in A2_06.
SCOPE:       the three C5 clauses A2_06 reported FAIL, measured at full precision on BOTH libraries.
GATE:        GATE.md 12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73 — unchanged,
             verified in this run's own provenance block.
RESULT:      T_peak, min Y_i and x_front ALL fail on the pre-fix library as well. Every one is a
             BROKEN CRITERION under §5.2, not a defect introduced by the locator fix.
EVIDENCE:    logs/A2_07_c5_precision.log  ·  logs/A2_07_c5_precision_PASS1_ABORTED.log (preserved)
PROBE:       tools/c5_precision_probe.cpp  cba98a9ad1892afb…  ·  tools/run_c5_precision.sh
REGRESSION:  NOT RUN. Nothing was changed to regress.
COMMIT:      none — not authorized.
```

---

## 1. Why this measurement was needed: C5 was never dry-run

`GATE.md` §1 records the dry-run as covering **"C1 and its controls N2/N3, against the UNCHANGED"**
library. C5 is not in that list. Under `CLAUDE.md` §5.2 — *"Run every criterion against the unchanged
baseline… A clause that was never executed is not 'passing'"* — C5's recorded `PASS (measured)` in
`verification.md` rests on **no baseline execution at all**.

Worse, its thresholds were transcribed from the baseline table's **print precision**:

```text
logs/04 printed   T_peak  1800.00     <- TWO decimals
                  x_front 3.50000     <- FIVE decimals, and it was the ANCHOR echoed back by the
                                         reporter TD-005 C4d later proved never measured anything
```

So `T_peak == 1800.00 K exactly` was never a measured property. It was a rounded print, promoted to
a bitwise-equality gate.

## 2. The instrument is sound before anything is read from it

**The reformatted tree reproduces A2_06 exactly**, which is what makes this run comparable:

```text
                A2_06        this run
steps           15283        15283         MATCH
T_peak          1799.99897   1799.99897    MATCH
min Y          -1.8898e-11  -1.8898e-11    MATCH
S_L             4.95533      4.95533       MATCH
```

TD-034's clang-format reformat is therefore **semantically inert**. Note what this does *not* rest
on: pass 1 of this harness required the rebuilt archive to equal A2_05's `43a24883` and **aborted**
when it gave `852555cf`. That abort is preserved. The cause was diagnosed, not assumed:

```text
(B) build nondeterminism  RULED OUT   three rebuilds of untouched source: 852555cf every time
(C) the tree changed      CONFIRMED   22 headers/sources reformatted 2026-10-08 23:43, AFTER
                                      A2_06 finished 23:34 -- PremixedFlame.hpp among them
```

`libcfdcore.a` is an **archive aggregating all 22 reformatted objects**, so its hash must change
even though formatting cannot change semantics. **The archive hash was never a valid instrument for
"is the reformat inert", and my earlier claim that it proved inertness used the wrong one.** The
valid instrument is reproducing the physics, which is what the table above does.

### 2.1 The reconstruction is corroborated on an independent fixture

The pre-fix leg removes **exactly one line** (asserted in the harness), per `CLAUDE.md` §6's first
admitted baseline form — an isolated copy with only the change removed. Against A2_05, which was run
at a **different fixture** (`timeStep` pinned to `9.4243e-06`, where this probe uses production dt):

```text
                         A2_05 recorded    this reconstruction
S_L cm/s                 4.671937          4.671936901          agrees to printed precision
front mm                 2.474665          2.474664271          agrees to printed precision
recentrings total/final  121 / 1           121 / 1              exact
fuelBalanceResidual      6.047382e-02      6.047150503e-02      DIFFERS -- and must
```

The balance residual **should** differ: it is dt-sensitive and the two fixtures pin dt differently.
The eigenvalue, the front and both counters agree. This is stated rather than smoothed because
"identical" was not measured and must not be claimed.

## 3. THE RESULT — both libraries, full precision

```text
                              PRE-FIX (baseline)        POST-FIX (locator fixed)
finalMarchSteps               17194                     15283
T_peak                        1799.999258440192762      1799.998974446609736
  1800.0 - T_peak             7.415598e-04 K            1.025553e-03 K
  == 1800.0 bitwise           NO                        NO
min Y_i                      -8.453045954736468e-12    -1.889798074490678e-11
  >= 0                        NO                        NO
x_front                       2.474664271154 mm         3.499219221840 mm
  offset from anchor         -51.267 cells              -0.039 cells
|sum Y - 1|                   9.300262713285434e-07     8.822866316426925e-07
S_L                           4.671936901 cm/s          4.955332027 cm/s
fuelBalanceResidual           6.047150503e-02           6.302972231e-03
recentrings total/final       121 / 1                   223 / 3
```

### 3.1 Clause by clause

```text
T_peak == 1800.00 exactly    BASELINE FAILS. 1799.99926, not 1800.0 bitwise. Both values print
                             "1800.00" at the two decimals the threshold was copied from.
                             -> BROKEN CRITERION (§5.2). Post-fix deficit is 1.38x larger
                                (1.03e-03 vs 7.42e-04 K) -- a real directional change, reported
                                in §4, and 6.8e-07 of the 1500 K rise.
min Y_i >= 0                 BASELINE FAILS. -8.45e-12, already negative.
                             -> BROKEN CRITERION (§5.2). Post-fix is 2.24x more negative.
x_front == 3.50000 exactly   BASELINE FAILS by 51.267 cells. Post-fix is 0.039 cells out --
                             1313x closer.
                             -> BROKEN CRITERION, and an OBSOLETE CONSTANT: it was the anchor
                                compared against itself.
|sum Y - 1| <= 1.2e-06       PASSES on BOTH, and post-fix is BETTER (8.82e-07 < 9.30e-07).
                             The 1.6224e-06 A2_06 recorded is at relaxT 90, a configuration C5
                             never judged; C5 is evaluated at the relaxT 10 reference, as C3 is.
```

**Every failing clause fails on the baseline. Not one of them is a regression caused by the fix.**

### 3.2 Two pre-registered predictions, both correct — and the one I had wrong

`c5_precision_probe.cpp` registered, before the run: *"pre-fix T_peak is ALSO 1799.99-something, and
pre-fix min Y is near -1e-11 rather than -1.5e-14."* Both held: `1799.99926` and `-8.45e-12`.

So `audit.md` §2's `min Y_i = -1.5e-14` belongs to **a different fixture** (the prior uncredited
work), and `audit.md` §5's `min Y_i = 0` is a rounded print. The two contradictory baseline records
are reconciled, and neither applies here.

**What I had wrong.** Before measuring, I described the post-fix `-1.89e-11` as a *1260x growth*,
against `audit.md`'s `-1.5e-14`. The real factor is **2.24x**, against the same fixture. Comparing a
measurement to a number from another fixture manufactured three orders of magnitude of alarm. The
growth is real but small, and it is not explained by march length — the post-fix march is **shorter**
(15283 against 17194 steps). It tracks the recentring count (3 against 1), which is a plausible
mechanism and is **not established here**.

## 4. What is NOT established

```text
NOT established  that the post-fix T_peak deficit (1.38x) and min Y (2.24x) are harmless. Both are
                 tiny and both are inside the bounds derived in §5, but "inside a bound I derived
                 after seeing them" is weaker than a pre-registered prediction, and §5's thresholds
                 must therefore be dry-run with controls that can FAIL before being frozen.
REFUTED 2026-10-09  why min Y_i tracks the recentring count -- IT DOES NOT. This section
                 offered the shiftField-fill count as a candidate. GATE_A3's dry-run tested
                 it directly at deadBand 1 on the post-fix library and |min Y| came back
                 1.1841e-11, SMALLER than deadBand 3's 1.8898e-11. The candidate is
                 withdrawn and NOTHING replaces it: the 2.24x post-fix change is
                 unexplained. What does track the recentring count is the composition SUM
                 (C5d: 8.8229e-07 -> 1.2355e-06). See dry_run_A3.md §5.2.
NOT established  which eigenvalue is correct. 4.955332 vs 4.671937 remains TD-040's question; this
                 run adds the pre-fix value at production dt and nothing else.
NOT measured     any negative control for the new T_peak and min Y bounds. The pre-fix library
                 PASSES both, so it is NOT a non-vacuity control for them. §5 says so explicitly.
NOT amended      C5. No threshold in GATE.md was touched. C5 stands FAILED.
NOT run          the regression. Nothing changed.
```

## 5. Replacements DERIVED from first principles — proposed, NOT frozen, NOT applied

`CLAUDE.md` §5.3 requires: preserve the failure (done, A2_06 and the aborted pass 1 are intact),
investigate without changing production (done), **derive the replacement from first principles,
never from the observed output**, then **request authorization** and freeze as a separate file with
its own dry-run and non-vacuity check. These are step 3 only.

### 5.1 `T_peak`

```text
|T_ad - T_peak|  <=  (T_ad - T_unburnt) * 1.2e-06  =  1500 K * 1.2e-06  =  1.8e-03 K
```

**Derivation.** `T_ad` is bitwise exactly `1800.0` from the case constants (measured in this run), so
the target is meaningful. The energy closure ties temperature to composition, mapping a composition
error into a temperature error with gain `(T_ad - T_u)`. C5 **already** bounds `|sum Y - 1|` by
`1.2e-06`, derived in `GATE.md` §3.2 as the measured baseline band of the tolerance-limited solve
that TD-003 owns. Propagating that same band through the same gain gives `1.8e-03 K`. No new constant
is introduced and nothing is read from the observed deficit.

```text
pre-fix  7.42e-04  INSIDE      post-fix  1.03e-03  INSIDE
```

### 5.2 `min Y_i`

```text
|min Y_i|  <=  linear acceptance fraction * Y_fuel,unburnt  =  1e-09 * 0.05  =  5e-11
```

**Derivation.** `GATE.md` §3.2 records `worstLinearResidual` **pinned at exactly the `1e-9`
acceptance fraction** — the solve is tolerance-limited, not round-off-limited. A relative linear
residual of `1e-9` on a quantity of scale `Y_F,u = 0.05` admits an absolute excursion of `5e-11`.
Round-off alone cannot explain the observation and is not used: `sqrt(15283) * 2.2e-16 * 0.05 ~
1.4e-16`, five orders too small. The bound comes from the solver's own documented acceptance.

```text
pre-fix  8.45e-12  INSIDE      post-fix  1.89e-11  INSIDE
```

### 5.3 `x_front`

```text
|x_front - x_anchor|  <=  (recentreDeadBandCells + 1) * dx  =  4 * 2e-05 m  =  0.08 mm
```

**Derivation.** `PremixedFlame.cpp:928` leaves the front alone while `|offsetCells| <
recentreDeadBandCells`, so the controller's **designed** steady-state error band is 3 cells. The `+1`
covers drift within the step in which the band is crossed, before the shift is applied: the
`admissibleJump` derivation gives `(u_burnt + S_L) * dt / dx = 1.234 / 8 = 0.154` cells per step, so
`+1` is ~6.5x conservative. Taken from the control law's own parameter, not from the measured
`3.499219 mm`.

```text
pre-fix  1.025 mm (51.267 cells)  FAILS by 12.8x      post-fix  0.00078 mm (0.039 cells)  INSIDE
```

### 5.4 The non-vacuity problem these three create, stated before it is asked

```text
x_front   the PRE-FIX library fails it by 12.8x -> it IS the negative control. Already measured.
T_peak    the pre-fix library PASSES (7.42e-04 < 1.8e-03). NOT a control.
min Y_i   the pre-fix library PASSES (8.45e-12 < 5e-11).  NOT a control.
```

So two of the three replacements **have no negative control yet**, and §5.2 forbids freezing a
criterion that cannot be shown to fail. Candidates, to be designed and dry-run rather than assumed:

```text
T_peak    a quenched flame. GATE.md §1 notes T_peak equals T_ad only while the flame is "not
          dying", so raising the activation energy, or N4's recentring-off case where the front
          leaves the domain, should drive T_peak well below T_ad.
min Y_i   recentreDeadBandCells = 1, the minimum the validator at :147 permits. It fires the
          operator on nearly every step, so if min Y_i tracks the fill count (§4) it should grow
          past 5e-11. This control also TESTS §4's unestablished mechanism, which is why it is
          the preferred one.
```

**If `deadBand = 1` does not push `min Y_i` past `5e-11`, the §4 mechanism is wrong and the bound
needs a different derivation** — that is a real falsifier, not a formality.
