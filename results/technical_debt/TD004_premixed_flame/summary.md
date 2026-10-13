# TD-004 — SUMMARY — **STOPPED AT C2. THE ITEM DOES NOT CLOSE.**

```text
item        TD-004   1D premixed-flame capability: recentring is non-conservative
priority    P1
route       (a) -- make recentring conservative (the only authorized route)
gate        GATE.md  sha256 12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73
              recomputed unchanged at the start of every step of this item
verdict     STOPPED at C2. measured ratio 46.9362 against the frozen <= 2.0.
            C2 was later found MIS-DERIVED and was AMENDED (GATE_A1.md, authorized 2026-10-05).
            C2-A1 was frozen with its own dry-run and rerun fresh: it also FAILS, but now for two
            NAMED open defects rather than against an impossible premise. See verification_A1.md.
            TD-004 remains OPEN / STOPPED either way.
status      OPEN / STOPPED.  Not resolved, not closed, not reverted.
stopped by  the user's instruction "stop TD-004 and preserve the findings", which
            followed this agent's own recommendation to stop.
```

**No threshold was weakened, no criterion dropped, no failure deleted, and the gate's sha256 is
unchanged from `logs/00_freeze.log`.**

---

## 1. Criteria, in frozen order

| # | criterion | threshold | measured | verdict |
| --- | --- | --- | --- | --- |
| C1 | the ledger is exact | `<= 8·eps·nc·max|integrand|` | established pre-freeze on the unchanged operator (`logs/05`) | PASS (pre-freeze) |
| **C2** | **the balance no longer degrades with march length** | **ratio `<= 2.0`** | **46.9362** — `1.531435e-02 -> 7.187978e-01` over march `3968 -> 35717`. Baseline was **14.6577** | **FAIL** |
| C3 | the balance closes at the reference configuration | `< 0.02` | `1.531435e-02`, from `4.327958e-02` | PASS (measured) |
| C4 | the balance is insensitive to the recentring RATE | every point `< 0.02` | `0.0153 / 0.6022 / 0.7188` | FAIL (measured) |
| C5 | what already works is preserved | `T_peak == 1800.00`; `x_front == 3.50000`; `min Y >= 0`; `|ΣY−1| <= 1.2e-06` | `1800.00` exactly · `3.50000` exactly · `min Y >= 0` · `5.96e-07 … 8.68e-07` | PASS (measured) |
| C6 | the eigenvalue keeps its qualified properties | each still holds; `S_L` reported with its change | `S_L = 4.86230 cm/s`, **unchanged**; property 3 flat at two anchor positions (§4.2) | PARTIAL (measured; properties 2 and 5 not re-run) |
| C6a | the refinement family re-derived on conserving states | `p` in `[1.5, 2.5]`, balance `< 0.02` at every level | — | **NOT REACHED** |
| C6b | the consumption integral agrees with the eigenvalue | agree to the march's own error | — | **NOT REACHED** |
| C7 | no silent change elsewhere | `CFDSpeciesTests` minus `G8PremixedFlameTest` = **343** | — | **NOT REACHED** |

Negative controls **N1–N5: NOT REACHED.** Regression (§5 of the gate): **NOT RUN.** Definition-of-done
clauses 3, 4, 4a, 5 and 6: **NOT REACHED.** Execution stopped at C2 per `CLAUDE.md` §1.3 and §5.3;
C3–C6 are recorded as *measured*, never as a basis for closing the item.

## 2. What the fix achieved, and it is substantial

The root cause named in `audit.md` was real, and every quantity it predicted would move, moved —
by one to two orders of magnitude.

```text
                            before fix        after fix       change
worst single-step JUMP      327.51 cells      5.13 cells      64x smaller
largest SHIFT applied       164 cells         4 cells         41x smaller
worst front offset          163.96 cells      3.04 cells      54x smaller  (dead band is 3.0)
recentrings at relaxT 90    32 669            241             135x fewer
fuel balance at relaxT 10   4.327958e-02      1.531435e-02    now UNDER the 0.02 gate (C3)
S_L                         4.86230 cm/s      4.86230 cm/s    unchanged (C6)
```

The locator was latching onto a crossing the front could not physically have reached, and executing
a 164-cell translation on the strength of it. That is unambiguously wrong, it is fixed, and the fix
did not move the eigenvalue. **The fix is kept, not reverted** — restoring it would put the
catastrophic shifts back and re-hide what C2 exposed.

## 3. Five successively refuted diagnoses

This is the part of the record worth more than any single number. **Each hypothesis fitted all the
data available when it was formed, and each was killed by the next measurement.**

| # | hypothesis | refuted by | the measurement that killed it |
| --- | --- | --- | --- |
| 1 | recentring is the root cause of the imbalance | `logs/09` | the content ledger explains 131 % of the imbalance at `relaxT 10` but only 24 % at `relaxT 90`, and is **not** proportional to the recentring count — 8 857 recentrings produced a *smaller* net change than 920. The shifts largely cancel, so "each shift leaks a cell" was wrong as an accumulation model |
| 2 | the profile has multiple `tAnchor` crossings | `logs/13` | `maxFrontCrossings = 1`. There is **never** more than one crossing. (This was `verification.md` §3's stated finding; it is marked **INVALID AS AUTHORITATIVE** in place there) |
| 3 | the admissible-jump bound is too tight | `logs/14` | only **108** of 4 441 rejections are under 10 cells, and that count is **identical at every march length** — a fixed startup artefact. **15 858** are over 50 cells |
| 4 | `seedProfile()` reconstructs the field mid-march | `logs/15` | **zero** rejections land on the anchor. The worst jump lands at `x = 0.1969 mm` — cell 9 of 500 |
| 5 | the inlet is igniting because it is too close | `logs/16` | upstream room `2.85 -> 4.07` preheat thicknesses changed the `>50c` count `3952 -> 3951`. The worst jump grew `168 -> 243` cells, i.e. **the same landing point**: the crossing relocates to the inlet regardless of how far away the inlet is |

`logs/16` was stopped after 2 of its 4 configurations, on the user's instruction. The refutation does
not need the other two — the stated falsification criterion was ">50c jumps falling to zero as
upstream room grows", and `3952 -> 3951` over a 43 % increase in upstream room is no change. The
annotation in that log records exactly what was not measured.

### 3.1 Why five refutations is itself the finding

Every one of these hypotheses was inferred from **counters derived from the temperature profile** —
jump magnitudes, rejection counts, landing positions — when the thing being inferred **is** the
profile. The instrument was downstream of the unknown. That is the methodological lesson, and it is
why the next step was put to the user instead of taken.

## 4. What is established, and is not in doubt

```text
* there is only ever ONE tAnchor crossing                               (logs/13)
* it relocates to within ~9 cells of the inlet on up to 46 % of steps   (logs/15)
* that is INDEPENDENT of the distance to the inlet                      (logs/16)
* the >50-cell jump count tracks the fuel balance exactly:
      0 jumps -> 1.53 % imbalance ; 3952 -> 60 % ; 15858 -> 72 %
* the march gets FURTHER from steady after the fix, not closer:
      dT residual  relaxT 10  1.6619e-01 -> 6.9730e-01
                   relaxT 90  3.7687e-01 -> 9.8982e-01
```

### 4.1 The remaining blocker — RESTATED TWICE SINCE THIS WAS WRITTEN

> **SECOND CORRECTION, 2026-10-05, from TD-005's falsification tests**
> (`TD005_flame_speed_extraction/logs/13`, recorded at that item's `dry_run.md` §H). The first
> correction below replaced "flashback" with "a front settling ~46 cells off-anchor". That is also
> incomplete: **the front OSCILLATES.** Measured on the admissible `mdot`, varying only the final
> march length:
>
> ```text
> relaxT 10   offset   +1.17 cells   fuel balance 1.531e-02
> relaxT 20   offset -163.08 cells   fuel balance 7.124e-01
> relaxT 30   offset -162.24 cells   fuel balance 6.022e-01   <- TD-004 measured 6.022411e-01
> relaxT 40   offset   +1.21 cells   fuel balance 1.049e-02   <- RECOVERS, best in either item
> ```
>
> **THE CONSEQUENCE FOR THIS ITEM'S C2.** C2 reads *"the balance no longer degrades with march
> length: ratio <= 2.0"*. There is **no monotone degradation** to bound: this item sampled relaxT
> 10/30/90 and caught one good phase and two bad ones. The measured `46.9362` was real and stopping
> was correct, but **no fix to the recentring operator could have satisfied that criterion**, because
> it bounds a trend that does not exist.
>
> Per CLAUDE.md §5.3 this is a mis-derived frozen criterion discovered after the freeze. The remedy
> is to preserve the original failure (done), investigate without changing production (done, under
> TD-005), derive a replacement from first principles, and **request authorization**. **No amendment
> is made here and C2 is not reclassified.** TD-004 stays OPEN / STOPPED.
>
> The first correction, and the original wording, are preserved below in order.

> **FIRST CORRECTION, 2026-10-05, by TD-005's field dump** — the flashback reading was refuted for
> the final state.

> **CORRECTED 2026-10-05 by TD-005's field dump**
> (`TD005_flame_speed_extraction/logs/02`), which printed the temperature and fuel fields instead of
> counters derived from them. In the bad state the **inlet sits at 300.18 K**
> (`(T[0]-T_u)/(T_ad-T_u) = 1.23e-04`) and the outlet fuel is at round-off (`3.07e-18`). **There is
> no flashback and no breakthrough; the final state is a physically sensible flame that reaches
> `T_ad = 1800.00 K` exactly.** The original reading is preserved below, per CLAUDE.md §4.
>
> **What the field shows instead:** the `1050 K` crossing sits at **2.537 mm**, i.e. **48.2 cells
> upstream of the 3.500 mm anchor** — off-anchor, but nowhere near the inlet. And
> `result.flamePosition` is **`anchorFraction * domainLength` echoed back**
> (`PremixedFlame.cpp:1725`, the sole assignment), so it reported `3.50000 mm` in both the good and
> the bad state. **Every `x_front` figure in this item's evidence is that echoed input, including
> `verification.md`'s C5 "x_front 3.50000 exactly".**
>
> **This reframes TD-004's refusal counters rather than contradicting them.** The front really had
> moved tens of cells; the admissible-jump bound correctly refused to translate the frame that far
> in one step; nothing then corrected the situation; and the reported position concealed it. The
> counters were right and the interpretation of them was wrong — twice.
>
> TD-004 remains **OPEN / STOPPED at C2**. What changes is the named blocker: not flame
> stabilisation, but a front that settles tens of cells off-anchor with no instrument able to see it.
> The reporting defect is **TD-005's** to fix (it owns the extraction method's reported quantities);
> whether fixing it resolves C2 is unmeasured and is not claimed here.

**The original text, preserved:**

> ```text
> The flame fills the domain, and the single 1050 K crossing relocates to the inlet --
> independently of how much upstream room it is given. That is FLASHBACK: a flame-
> STABILISATION problem, not a recentring, locator or boundary-proximity problem.
> ```
>
> For the *only* crossing to sit nine cells from a Dirichlet boundary pinned at 300 K, everything
> downstream of it must be above `tAnchor`. Nothing in route (a) — the only authorized route —
> touches flame stabilisation. Fixing it needs an eigenvalue formulation that does not depend on
> locating a single crossing, which is **TD-005's** territory, and TD-005 is itself OPEN and
> unauthorized.

### 4.2 A genuine side-result, recorded because it is evidence for a criterion that cannot run

`S_L = 4.86230 cm/s` at `anchorFraction` **0.35 and 0.50** — identical to six significant figures.
That is `audit.md` property 3's own criterion (`S_L` plateaus in the anchor position, the
cold-boundary difficulty) passing at two points, for a test the suite currently **cannot run at all**
because `TheEigenvaluePlateausInTheAnchorPositionAndTheDomain` times out at 900 s. Two points are not
a plateau; they are two points, and they are recorded as such.

### 4.3 The fix's ~2x cost — **this section's original risk claim was WRONG**

> **CORRECTED 2026-10-04, during TD-005's audit.** This section originally predicted that the fix
> was "likely to fail clause 4a on performance alone", on the basis that three tests already time
> out "at 900 s". **The 900 s cap is TD-004's own probing harness, not this repository's.** The
> original wording is kept below the correction, per CLAUDE.md §4.

Measured during TD-005's audit:

```text
tests/unit/species/CMakeLists.txt:37  gtest_discover_tests(...)  -> each case is its own
                                                                    ctest entry
.github/workflows/ci.yml:147          ctest ... --timeout 7200
no per-test TIMEOUT property exists anywhere in the CMake tree
TD004_premixed_flame/logs/06 line 5   "per-test timeout 900s"   <- the harness, and the
                                                                    log says so itself
```

So the repository's cap is **7200 s per test**, the longest baseline test is **804 s**, and the
headroom is about **9x** — not thin. A ~2x locator cost does not threaten it. The three recorded
"TIMEOUT after 900s" verdicts are harness artefacts and were never repository failures; whether any
of those tests actually exceeds 7200 s is **still unmeasured**, and TD-005's gate §5.2 now requires
measuring it rather than naming it.

**What remains true:** the fix does cost ~2x wall time (507 s per configuration against ~250 s, and
~6x before the early-out). That is a real cost and is worth knowing. It is simply not a
definition-of-done failure.

**The original text, preserved:**

> The locator change costs ~2x wall time after the early-out — 507 s per configuration against a
> ~250 s baseline, and ~6x before the early-out was added. Gate clause 4a requires that
> `WithReactionsDisabledNoFlamePropagates` still time out and that no OTHER test in the suite
> acquire a timeout. Three already time out at 900 s on the unchanged tree (`logs/06`), and tests at
> 744 s, 804 s and 561 s have little headroom. So the fix as written is likely to fail clause 4a on
> performance alone. This was not measured — the run stopped at C2 before the regression — and is
> recorded as a predicted risk, not a result.

## 5. Files changed, and their disposition

```text
PRODUCTION  src/species/PremixedFlame.cpp         locateFront rewritten: nearest-crossing
                                                  tracking + an admissible-jump bound derived
                                                  from mdot/min(rho) + mdot/rho_u times
                                                  kJumpMargin = 8, with an early-out on the
                                                  first admissible crossing; recentre now
                                                  REFUSES shifts beyond the dead band + bound
            include/cfd/species/PremixedFlame.hpp diagnostic fields on PremixedFlameResult
TEST        tests/unit/species/test_premixed_flame.cpp
                                                  refinement triplet {250,500,1000} ->
                                                  {125,250,500}, derivation in-comment, marked
                                                  PROVISIONAL; DISABLED_BalanceVersusMarchLength's
                                                  knob corrected to finalMarchRelaxationTimes with
                                                  an EXPECT_TRUE(moved) fail-closed guard
EVIDENCE    results/technical_debt/TD004_premixed_flame/{GATE,audit,verification,summary}.md
                                                  logs/00..16, tools/{balance,ledger_dryrun,
                                                  fixture,upstream}_probe.cpp
```

No commit and no push were made; none was authorized. The three files above are **untracked** in an
already-uncommitted tree (132 tracked files carry real diffs at `HEAD 3bd32f9`), so this item
produced no `git diff` of its own to record. `git diff --check` reports pre-existing trailing-space
warnings beyond TD-018's three CRLF files; that is gate clause 4, which was **NOT REACHED**, and is
TD-018's to resolve.

### 5.1 A defect this item INTRODUCED and then caught with its own guard

The fix's first version (`logs/11`) **broke the solver**: `S_L = 29.56138 cm/s` against `4.86230`,
a **0-step** final march, fuel balance `9.49e-01`. Cause: `previousLocated` / `haveLocated` had
function scope, so the locator's tracked position outlived the profile it tracked — every trial calls
`seedProfile()`, which discontinuously returns the front to the anchor, and the stale position then
made that legitimate front look like an inadmissible jump. A correctness check given state with the
wrong lifetime. Corrected by forgetting the tracked front at all three re-seed sites.

It was caught by the probe's own fail-closed guard (`*** PROBE INVALID: the final march length did
not change`), which exists only because an **earlier version of that same probe printed a false
plateau** by sweeping `finalMarchSteps` — a 200 000 safety ceiling — instead of
`finalMarchRelaxationTimes`. Three identical rows had read as convergence. The guard that caught the
broken solver was added in response to that.

## 6. Reported, deliberately NOT fixed

* **`unused variable 'seedSpeed'`** at `src/species/PremixedFlame.cpp:708` — pre-existing, outside
  TD-004's scope (`CLAUDE.md` §2: do not fix unrelated defects inside another phase).
* **The diagnostic fields added to `PremixedFlameResult`** (`recentringFuelLedger`,
  `recentringEnergyLedger`, the `*LedgerRate` pair, `worstFrontJumpCells`, `largestShiftCells`,
  `rejectedFrontCrossings`, `rejectedShifts`, `maxFrontCrossings`, `worstRejectedJumpCells`,
  `rejectedJumps{Under10,10To50,Over50}Cells`, `worstRejectedJumpPosition`,
  `rejectedJumpsLandingAtAnchor`) were added as instruments. C1's ledger pair is load-bearing; the
  rest would each need justification before being kept permanently in a public result struct.
* **`farthestRejectedPosition`**, an earlier diagnostic, was **incoherent** — it compared a new
  distance against one recomputed from a stale position relative to the *current* front, was never
  updated, and read `0.0000 mm` throughout. Replaced by the bucketed distances. `logs/13`'s row
  carries that `0.0000 mm`; it is an instrument defect, not a measurement.

## 7. What the next step would be, if TD-004 is reauthorized

Not a sixth hypothesis inferred from profile-derived counters. **Dump the temperature field at a step
where a `>50c` jump occurs and look at it directly** — that distinguishes a flame that has genuinely
propagated to the inlet from a profile defect that merely looks like one, and it is the first
measurement in this chain whose instrument is not downstream of the unknown.

That is a new line of work. It was put to the user rather than started.
