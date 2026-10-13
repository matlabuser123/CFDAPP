# TD-004 — C2-A1 RE-MEASURED on the fixed tree · **FAILS at clause (a)**

```text
date        2026-10-07
gate        GATE_A1.md  sha256 e57bc758ca5610eb…  VERIFIED unchanged before the run
probe       tools/c2a1_probe.cpp  sha256 83a6c5d843466ba9…  VERIFIED unchanged, NOT edited
tree        PremixedFlame.cpp 8c85bc9f5f457bf2…  library be2520b7fb562931…
            = TD-027's cap 5 + TD-032's time-level fix + TD-037's drift-window clamp
logs        A1_03_c2a1_remeasure.log · A1_04_c2a1_repeat.log
            A1_02_c2a1_remeasure_PARTIAL.log — truncated by a WSL VM restart, preserved
verdict     C2-A1 FAILS at (a) REACHES. (b) and (c) are NOT EVALUABLE — empty domain.
            TD-004 remains OPEN / STOPPED, but its blocker has CHANGED.
```

This does not supersede `verification_A1.md`, which records the original measurement and stands as
history (CLAUDE.md §4). It records a **re-measurement of the same frozen criterion on a changed
tree.**

---

## 1. The measurement, and its repeat

```text
relaxT   marchStp   fuel bal     admiss   S_L cm/s   delta mm
    10     17194    6.047e-02      NO     4.67194    1.1844
    14     24072    7.142e-02      NO     4.67194    1.1818
    18     30949    8.201e-02      NO     4.67194    1.1795
    22     37827    8.887e-02      NO     4.67194    1.1774
    26     44705    9.216e-02      NO     4.67194    1.1765
    30     51583    9.350e-02      NO     4.67194    1.1761
```

**`A1_04` reproduces `A1_03` identically — all six rows, all six columns.** Two independent
invocations of the same frozen probe against the same library.

## 2. The probe STOPPED, and its guard fired on an obsolete constant

```text
pattern BBBBBB      logs/14 measured GGBGGB
*** PATTERN DIFFERS FROM logs/14. The determinism this amendment rests on is in question.
    STOPPING: a criterion must not be evaluated against a non-reproducible signal.
```

The guard compares the admissibility pattern against a **hardcoded `GGBGGB`** and reads any
difference as non-determinism. That was correct when written: `GGBGGB` was the recorded signature and
a departure from it would have meant the signal could not be trusted.

**It is now an obsolete CONSTANT, not a live detector**, and the distinction was settled by
measurement rather than by argument: the guard's premise is *"the signal is not reproducible"*, and
the repeat run shows it reproduces exactly. The pattern differs because the **solver was fixed** —
TD-027's cap, TD-032's time level, TD-037's drift window — not because it wanders.

**The probe was NOT edited.** Changing a frozen instrument's constant is an amendment under
CLAUDE.md §5.3 and needs its own authorization; it is carved out rather than done here. Same
distinction as TD-005's obsolete-instrument finding: separate an obsolete constant from a
contaminated instrument, and do not quietly repair either inside another item's measurement.

## 3. C2-A1, clause by clause — and why only one is evaluable

```text
(a) REACHES   at least one level has fuelBalanceResidual < 0.05
              -> NO LEVEL DOES. Lowest is 6.047e-02 at relaxT 10.          FAIL
(b) STAYS     every level at or beyond the first admissible one
              -> there is no first admissible level.          NOT EVALUABLE, empty domain
(c) SETTLES   over the levels at or beyond it, (max - min) <= mean
              -> same empty domain.                           NOT EVALUABLE
```

`GATE_A1.md` §3.1 anticipated this shape exactly: *"(b) alone would be vacuously satisfiable by a
family in which nothing is ever admissible, since 'every level at or beyond the first admissible
one' is an empty condition."* **Reporting (b) or (c) as PASS here would be claiming a vacuous
criterion as evidence**, which is the error that produced this amendment in the first place.

**Separately, and NOT as clause (c)** — because its domain is empty — the settling behaviour over
the full family is worth recording:

```text
max 9.350e-02   min 6.047e-02   max-min 3.303e-02   mean 8.141e-02   ->  max-min < mean
increments:  +1.095  +1.059  +0.686  +0.329  +0.134   (e-02, monotone shrinking)
```

The balance is **monotone and converging**, apparently toward ~9.4e-02.

## 4. What changed against the recorded baseline, and it cuts both ways

```text
                    BEFORE (cap 20, alternating)      NOW (fixed tree)
pattern             G G B G G B                       B B B B B B
delta mm            1.229 1.224 0.411 1.223 1.223     1.184 1.182 1.180 1.177 1.177 1.176
                    0.419                             — monotone, no flip
balance at relaxT 10  1.531e-02  (admissible)          6.047e-02  (not admissible)
S_L cm/s            4.86230 at every level            4.67194 at every level
```

**The alternation is gone.** `delta` no longer flips between 1.22 mm and 0.41 mm; it decreases
monotonically. This is the seventh independent family in which TD-032's defect has been shown
removed, and it is the cleanest: a monotone ladder where there was a square wave.

**But the previously ADMISSIBLE readings were artefacts too, not only the failing ones.** At relaxT
10/14/22/26 the pre-fix balance read 1.0–1.5e-02 and passed; the true non-alternating value at those
same levels is 6.0–8.9e-02 and fails. The cold branch of the alternation had been producing
good-looking balances. **Every "good" reading in `logs/14`'s GGBGGB was as unreliable as every "bad"
one** — which is a stronger statement than TD-032's write-up made, and it is recorded here because
it bears on how much of the premixed-flame evidence base survives.

`S_L` moving 4.86230 → 4.67194 is TD-027's cap change, already established, not a new effect.

## 5. TD-004's blocker has CHANGED, and that is the useful result

```text
BEFORE   C2-A1 failed at (b) and (c): the state DEPENDED on where the march stopped, because the
         march alternated. Named prerequisites: TD-028 and TD-030.
NOW      C2-A1 fails at (a) alone: the state is becoming march-length INDEPENDENT -- monotone,
         converging, max-min below the mean -- but sits about 1.9x above the tree's own 0.05
         conservation bound.
```

So the march-length dependence that C2 existed to catch is **substantially repaired**, and what
remains is an **absolute conservation deficit**. That is TD-030 — the reaction-integral excess —
which `GATE_A1.md` named as a prerequisite before any of this was measured.

**TD-028's phenomenon is measured gone** at this fixture, which is a status question for its own
register entry rather than something this re-measurement closes.

## 6. Carved out, not done here

```text
TD-039  `c2a1_probe.cpp` hardcodes the pre-fix admissibility pattern `GGBGGB` and refuses to issue
        a verdict when the measured pattern differs. On a fixed solver the pattern is SUPPOSED to
        differ, so the guard now blocks the very re-measurement the amendment was written to enable.
        The guard's intent -- refuse to judge a non-reproducible signal -- is right; its
        implementation tests the wrong thing. It should compare a run against a REPEAT of itself,
        which is what was done by hand here. Replacing the constant is a §5.3 amendment.
```

## 7. What this does NOT establish

```text
NOT closed    C2-A1, and therefore not TD-004. It fails, for a named and different reason.
NOT measured  TD-027's C4, TD-029, TD-030, TD-031 -- still to be re-measured on the fixed tree.
NOT validated any flame speed. G8 is untouched.
NOT run       any regression for this re-measurement; it changed no production code.
```

---

## 8. WHY clause (a) fails — the inlet term, moved here from TD-030 on 2026-10-08

TD-030's audit re-measured TD-005's fuel budget on this tree and found that the gap clause (a) trips
on is **not** mostly a chemistry deficit. Evidence:
`technical_debt/TD030_split_order/audit.md`, logs `01` and `03`.

```text
term                 §O recorded (alternating)   now, relaxT 22    now, relaxT 26
reaction               -107.892 %                  -103.922 %        -103.878 %
inlet diffusion          -0.572 %                    -4.965 %          -5.338 %
```

**The inlet diffusive term grew ninefold and is now the larger of the two.** It is
`gamma·(Y_F[0] − Y_F,u)/(dx/2)` — a function of one number, the first cell's fuel deficit — and that
deficit is set by where the flame sits:

```text
relaxT   front mm   delta mm   deficit        inletDiff%
    10   2.474665   1.184404   -1.1784e-05      -1.784
    30   1.775956   1.176144   -3.6258e-05      -5.490
```

The front travels **0.699 mm upstream** while `delta` holds essentially constant (1.1844 → 1.1761).
**The flame translates; it does not broaden.**

### 8.1 The flame settles ~87 cells upstream of its anchor, and the fixture is sized for the anchor

```text
anchor                       3.500 mm
front converges to          ~1.77 mm          about 87 cells upstream
designed upstream room       3.5 / 0.6 = 5.8 preheat lengths   (the fixture's own comment)
actual upstream room         1.78 / 0.6 = 2.96 preheat lengths
```

`test_premixed_flame.cpp`'s sizing comment reads *"x_anchor = 0.35 * 10 mm = 3.5 mm = 5.9
delta_preheat, enough for the plateau"*. That sizing is sound **for a flame at the anchor.** The
flame is not at the anchor, so it has roughly half the upstream room the fixture assumed, and the
preheat tail reaches the inlet and depletes the first cell.

**This is the real, smaller residue of TD-004's original "flashback" reading.** The larger part of
that reading was TD-032's alternation putting the reported front at the inlet; what remains after the
fix is a genuine 1.73 mm displacement. So the classification that stopped TD-004 — flame
**stabilisation** — was right in kind, and wrong in magnitude by about an order.

### 8.2 The front has not converged at 51 583 steps

```text
front decrements over relaxT 10->30:  0.328  0.207  0.104  0.044  0.017 mm
```

Still translating, decrements halving, extrapolating to ~1.765 mm. **This is why the C2-A1 ladder's
balance residual kept climbing** (6.047e-02 → 9.350e-02): the inlet term rises because the front is
still moving. Recorded against TD-031 as well — a march-length criterion needs to watch the front,
not only the per-step field change.

### 8.3 What this changes about TD-004

Clause (a) fails because the balance residual sits ~1.9x above the tree's own 0.05 bound, and that
residual is now **mostly an anchoring effect owned by this item**, not the chemistry deficit owned by
TD-030:

```text
~5.5 %  inlet diffusive flux, from a front 87 cells off its anchor   -> TD-004
~3.9 %  reaction-integral excess                                      -> TD-030
```

TD-004 remains OPEN / STOPPED. Its blocker is now quantified rather than named: the flame stabilises
1.73 mm upstream of the anchor and takes more than 51 583 steps to get there.
