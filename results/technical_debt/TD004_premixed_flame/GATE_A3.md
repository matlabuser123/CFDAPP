# TD-004 GATE AMENDMENT A3 — C5 only

```text
amends        C5 of GATE.md (12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73)
authorized    2026-10-09, all three clauses, after the §5.3 investigation in verification_A3.md
supersedes    C5's T_peak, x_front and min Y_i clauses ONLY
leaves intact C1, C2 (as amended by GATE_A1.md), C3, C4, C6, C6a, C6b, C7, and C5's
              |sum Y - 1| <= 1.2e-06 clause, which PASSES on both libraries and is NOT touched
status        DESIGN. NOT FROZEN. The dry-run and non-vacuity check come first (CLAUDE.md §3
              steps 6-7), and only then is this file's sha256 frozen in logs/A3_00_freeze.log.
```

---

## 1. Why C5 is being amended rather than simply failed

`GATE.md` §1 records the dry-run as covering *"C1 and its controls N2/N3, against the UNCHANGED"*
library. **C5 is absent from that list and was never executed against the baseline.** Under
`CLAUDE.md` §5.2, a clause that was never executed is not passing, so C5's recorded
`PASS (measured)` rests on nothing.

`verification_A3.md` then measured all four clauses on **both** libraries at full precision. Three
fail on the **baseline**:

```text
                             PRE-FIX (baseline)      POST-FIX        baseline verdict
T_peak == 1800.00 exactly    1799.999258440          1799.998974446  FAILS (not bitwise 1800)
min Y_i >= 0                -8.453045954736e-12     -1.889798074e-11 FAILS (already negative)
x_front == 3.50000 exactly   2.474664271 mm           3.499219222 mm  FAILS by 51.267 cells
|sum Y - 1| <= 1.2e-06       9.300262713e-07          8.822866316e-07 PASSES -> NOT amended
```

`CLAUDE.md` §5.2: *"A sanity criterion that the baseline fails is a broken criterion."* All three are
broken. Their origin is documented and is the same in each case — **they were transcribed from the
baseline table's print precision, not from a measurement**:

```text
logs/04 printed  T_peak  1800.00   two decimals; both libraries print 1800.00 at two decimals
                 x_front 3.50000   five decimals, and it was the ANCHOR ECHOED BACK by the
                                   reporter TD-005's C4d proved never measured anything
```

Per §5.3 this is handled by amendment, not by re-thresholding in a probe, and each replacement is
**derived from first principles, never from the observed output**.

## 2. The amended criteria

| id | criterion | quantity | threshold |
| --- | --- | --- | --- |
| C5a | the burnt plateau reaches the adiabatic temperature to within the solve's own tolerance | `|T_ad - T_peak|` | `<= (T_ad - T_unburnt) * 1.2e-06 = 1.8e-03 K` |
| C5b | no species mass fraction goes negative beyond the linear solve's acceptance | `|min Y_i|` | `<= 1e-09 * Y_fuel,unburnt = 5e-11` |
| C5c | the front is held inside the recentring controller's designed error band | `|x_front - x_anchor|` | `<= (recentreDeadBandCells + 1) * dx`, which is `0.08 mm` at the reference `deadBand = 3` |
| C5d | composition sums to one within the measured baseline band — **UNCHANGED from C5** | `|sum Y - 1|` | `<= 1.2e-06`, at the relaxT 10 reference configuration |

All four are evaluated at the **relaxT 10 reference configuration**, the same configuration C3 names.
This is stated because C5 never said so, and A2_06's probe chose relaxT 10 by implication: the
`1.6224e-06` it reported at relaxT 90 was therefore never a C5 measurement, and C5d does not gate it.
`|sum Y - 1|` creeping with march length is `GATE.md` §3.2's tolerance-limited solve and stays
attributed to TD-003.

### 2.1 C5a — derivation

`T_ad` is **bitwise exactly `1800.0`** when computed from the case constants
(`T_u + Y_F,u * (-h_f,P / W) / c_p`), measured in `logs/A2_07`. So an equality target is meaningful in
principle; what was wrong was requiring the *discrete march's maximum* to hit it exactly.

The energy closure ties temperature to composition, carrying a composition error into temperature
with gain `(T_ad - T_unburnt)`. C5 **already** bounds `|sum Y - 1|` by `1.2e-06`, derived in
`GATE.md` §3.2 as the measured baseline band of the tolerance-limited solve. Propagating that
existing band through that gain:

```text
1500 K * 1.2e-06 = 1.8e-03 K
```

**No new empirical constant is introduced**, and nothing is taken from the measured deficits
(`7.42e-04` pre-fix, `1.03e-03` post-fix), both of which fall inside.

### 2.2 C5b — derivation

`GATE.md` §3.2 records `worstLinearResidual` **pinned at exactly the `1e-9` acceptance fraction**:
the solve is tolerance-limited, not round-off-limited. A relative linear residual of `1e-9` on a
quantity whose scale is `Y_F,u = 0.05` admits an absolute excursion of `5e-11`.

Round-off is explicitly **not** the basis, because it is five orders too small to explain the
observation and would give a vacuously tight bound:

```text
sqrt(15283) * 2.2e-16 * 0.05  ~  1.4e-16
```

Measured: `8.45e-12` pre-fix, `1.89e-11` post-fix. Both inside.

### 2.3 C5c — derivation

`PremixedFlame.cpp:928` leaves the front alone while `|offsetCells| < recentreDeadBandCells`, so the
controller's **designed** steady-state error band is `recentreDeadBandCells = 3` cells. The `+1`
covers drift during the step in which the band is crossed, before the shift is applied: the
`admissibleJump` derivation gives `(u_burnt + S_L) * dt / dx = 1.234 / 8 = 0.154` cells per step, so
`+1` cell is about 6.5x conservative.

```text
(3 + 1) * 2e-05 m = 8e-05 m = 0.08 mm
```

Taken from the control law's own parameter. The measured `3.499219 mm` is not used.

**The bound SCALES with the dead band in force**, because the quantity it bounds is set by that
parameter: at `deadBand = 1` it is `(1 + 1) * dx = 0.04 mm`, not `0.08 mm`. A fixed `0.08 mm`
would be vacuously loose for M2. The gate verdict is taken at the reference `deadBand = 3`.

**The absolute value is deliberate in C5a and C5b.** An overshoot above `T_ad`, or a mass
fraction above its unburnt value, is as wrong as an undershoot; a one-sided test would let one
of those through. Both measured values happen to be one-signed, which is exactly the situation
in which a one-sided test looks adequate and is not.

## 3. Dry-run and non-vacuity — REQUIRED BEFORE FREEZING

`CLAUDE.md` §5.2 requires every criterion to be executed against the baseline **and** to be shown
capable of failing. The baseline alone does not suffice here, and that is stated plainly:

```text
C5c   the PRE-FIX library fails it by 12.8x        -> the baseline IS its negative control
C5a   the pre-fix library PASSES (7.42e-04 < 1.8e-03)   -> NOT a control. One is required.
C5b   the pre-fix library PASSES (8.45e-12 < 5e-11)     -> NOT a control. One is required.
C5d   carried over unchanged, but it was NEVER DRY-RUN either, so it is dry-run here
```

### 3.1 The controls

| id | control | kind | must produce |
| --- | --- | --- | --- |
| M1 | the **pre-fix** library (locator fix reverse-applied, exactly one line) | baseline | **C5c FAILS**; C5a, C5b, C5d PASS |
| M2 | `recentreDeadBandCells = 1.0`, the minimum the validator at `:147` permits | settings only, **MECHANISM TEST** | no required outcome — it tests whether `min Y_i` grows with the recentring count. Pass 1's invalid leg said it does **not** (6.20e-12 at deadBand 1 against 8.45e-12 at deadBand 3). Recorded either way |
| M4 | the species update at `:309` scaled by `1.0001` | corrupted operator, reverted | **C5b FAILS** — over-integrating drives `Y_fuel` below zero at the trailing edge by ~`5e-06`, against a `5e-11` bound. Scaling every species by one factor keeps `sum(sources) = 0`, so C5d is not collaterally corrupted |
| M3 | the heat-release source scaled by `0.9999` in the library, `T_ad` left untouched | corrupted operator, reverted | **C5a FAILS** — the plateau equilibrates near `T_u + 0.9999 * 1500 = 1799.85 K`, which is `0.15 K` low and misses `1.8e-03` by ~83x |

M3 and M4 are temporary production edits of the same kind as `GATE.md`'s N2 (a sign-flipped
ledger) and are reverted by the harness under an EXIT trap with a hash check. M2 needs no
production change.

**Why M4 was added after the design was written.** Dry-run pass 1 found that **C5b never failed
on any leg**, and that M2 pointed the opposite way from the mechanism §3.2 registered. Under
§5.2 a criterion that cannot be shown to fail may not be frozen, so C5b needed a control that
definitely bites. Choosing an *instrument* empirically is legitimate; a **threshold** must never
be fitted to observation, and C5b's `5e-11` is unchanged — it still comes from the linear
acceptance fraction times the fuel scale, derived before any of this was measured.

### 3.2 M2 is a REAL FALSIFIER, not a formality

`verification_A3.md` §4 records as **NOT established** that `min Y_i` tracks the recentring count.
M2 tests exactly that: `deadBand = 1` fires the operator far more often than `deadBand = 3`.

```text
if M2 pushes |min Y_i| past 5e-11   C5b is non-vacuous AND the fill-count mechanism gains support
if M2 leaves |min Y_i| ~ 1e-11      the mechanism is WRONG, C5b has no control, and C5b must be
                                    re-derived before it can be frozen. This outcome STOPS the
                                    amendment at C5b; it is not worked around.
```

**No prediction is registered between those two.** I have been wrong on a pre-registered value three
times in this campaign and right twice; this is recorded either way.

## 4. What this amendment does NOT do

```text
does NOT relax  |sum Y - 1|. C5d keeps 1.2e-06 exactly, and the suite's own failing 1e-8 test stays
                failing and stays attributed to TD-003. GATE.md §3.2 is not reopened.
does NOT touch  C1, C2/C2-A1, C3, C4, C6, C6a, C6b, C7. C3 and C4 keep their A2_06 PASS.
does NOT close  TD-004. C6, C6a and C7 have never been run, and TD-040 carries the S_L +6.07 %
                stop independently of C5. Passing C5a-C5d advances the gate to C6 and nothing more.
does NOT claim  that the post-fix T_peak deficit (1.38x the baseline's) and min Y_i (2.24x) are
                harmless. They are inside bounds derived AFTER they were seen, which is weaker than
                a pre-registered prediction -- which is precisely why M1-M3 must run first.
does NOT re-run C3 or C4. Their values are recorded in A2_06 against this same tree, which
                logs/A2_07 proved reproduces A2_06 exactly.
```

## 5. Execution order, and the stop rule

```text
1  dry-run: M1, then C5a-C5d on the post-fix library, then M2, then M3
2  non-vacuity: each of C5a, C5b, C5c must have FAILED on at least one control
3  ONLY THEN freeze this file's sha256, with the source and library hashes, in logs/A3_00_freeze.log
4  re-run C5a-C5d fresh on the post-fix library and record the verdict
```

**Stop at the first control that does not produce its required outcome.** A control that fails to
fail is a vacuous criterion, and `CLAUDE.md` §12 requires stopping and reporting rather than
proceeding to the verification run.
