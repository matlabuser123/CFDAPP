# TD-032 ATTRIBUTION — the PASS → FAIL on `TheAnswerIsInsensitiveToTheStepsPerTrial`

```text
run        logs/06_attribution.log      2026-10-07T03:30 -> 03:52
method     two libraries, CLAUDE.md section 6; both source forms hash-proved before building
verdict    CAUSED -- the fix caused the transition. My hypothesis was REFUTED.
caveat     "caused the transition" is NOT the same claim as "lost a capability". See section 4.
```

## 1. The measurement

```text
                 source sha256 (proved)        library                       result
pre-fix    dac2992006a51cce6ea9e9f497dc...  3b94eb849f9c8277beded611db6...   PASS   1311 s
post-fix   f9f554bf9e1fe1fd29f6ce4c185e...  a9b01668be7ccb7f085f0dc65da...   FAIL    663 s
```

The pre-fix library is **byte-identical to the frozen lagged library** recorded in
`logs/00_freeze.log` and measured by the dry run, so the baseline is not a lookalike: it is the same
library. The reconstruction was produced by `tools/swap_td032.py`, which refuses to write anything
whose sha256 does not match the frozen value — a guard that exists because an earlier reverse-apply
in this campaign dropped one inserted blank line and produced a plausible-but-wrong baseline.

**Pre-fix:** all three drift windows (16, 32, 64) converge, `drift-window spread: 0` exactly.
**Post-fix:** windows 16 and 32 converge identically; window 64 returns
`PremixedFlameStatus::NoFlame` (2) and `ASSERT_EQ(flame.status, Converged)` at
`test_premixed_flame.cpp:725` aborts the test.

The recorded pre-fix PASSes this confirms: **744 s** in `TD004_premixed_flame/GATE.md:220` and
`logs/06`; **759 s** in `TD005_flame_speed_extraction/logs/08` and `summary.md:170`.

## 2. What else moved between the two libraries

```text
              S_L        S_c      S_c/S_L     delta       steps (window 16)
pre-fix    4.67194    4.19794      0.899     0.982081 mm    43893
post-fix   4.67194    4.87111      1.043     1.18440  mm    32219
```

`S_L` is **identical to six figures**, which is TD-032's C5 confirmed a second time and now by a
direct two-library pair rather than by a before/after of the same tree.

The **consumption identity moved from 10.1 % BELOW `S_L` to 4.3 % ABOVE** — smaller in magnitude and
opposite in sign. A flame that undershoots its own consumption identity by 10 % is not a converged
flame, so this is weak support for the reading in section 4, and it is a **lead for TD-030** (whose
reaction-integral excess was recorded at 107.892 %). It is not evidence for either here, and no
claim is made from it.

## 3. My pre-registered hypothesis is REFUTED

Registered in `tools/run_attribution.sh` before the run:

> **UNMASKED** — the configuration never had a flame, and the pre-fix alternation hid that. The hot
> branch was a nearly fully burnt field, which crosses `T_anchor` everywhere, so `locateFront`
> succeeded on every hot-branch step and a flame was always "found".

**Dead.** It predicted the pre-fix tree would also fail. The pre-fix tree passed, cleanly, with a
spread of exactly zero at all three windows.

## 4. MY PRE-REGISTRATION WAS ITSELF DEFECTIVE, and this is the more useful finding

The outcome map read `CAUSED -> capability regression -> BLOCKED`. That conflates two claims:

```text
(a) the fix caused the PASS -> FAIL transition            ESTABLISHED by this measurement
(b) the fix removed a capability the solver had           NOT established
```

A third reading exists that the map did not allow for, and this measurement **cannot discriminate
it**:

```text
(c) the fix turned a FALSE pass into a TRUTHFUL failure
```

(c) is live because the pre-fix PASS was computed on the alternating march. The hot branch is a
nearly fully burnt field; it crosses `T_anchor` everywhere; so on the pre-fix tree a "flame" is
found whatever the physical state. A PASS obtained that way is not evidence that a flame existed at
window 64 — it is evidence that the pre-fix instrument could not report its absence.

So the honest position is: **the fix caused the transition, and whether that transition is a loss or
a correction is OPEN.**

**This is the third time in this campaign that a falsifier has been named per PREDICTION rather
than per DIRECTION.** The earlier two are recorded in
`TD005_flame_speed_extraction/dry_run.md` section A.5 and in
`TD031_final_march_length/` — in both cases the hypothesis died by a direction that had not been
nominated. Here the hypothesis died correctly, but the *verdict* map was still too coarse to carry
the result.

## 5. What would discriminate (b) from (c) — NOT run, not authorized

```text
1  dump the post-fix field at window 64 and ask whether T crosses T_anchor ANYWHERE.
   If it does not, NoFlame is a truthful report and (c) holds.
2  dump the pre-fix field at window 64 at the step the locator accepted, and check whether the
   accepted crossing is on the cold branch or the hot one. A hot-branch crossing makes the
   pre-fix PASS an artefact directly, rather than by inference.
3  measure where window 64 differs from 32 at all: the only knob is the drift window, which
   changes how long trials run before a drift is read, so the divergence is in the SEARCH, not
   in the final march.
```

Step 2 is the decisive one and it is cheap — it needs no production change, because the field at a
chosen step is already reachable by the budget-ceiling technique used in
`TD031_final_march_length/tools/cell35_history_probe.cpp`.

## 6. Consequence for TD-032

The frozen gate's criteria are unaffected: **C0–C6 and C8 PASS, C7 PASSES in full.** This failure is
not one the gate names, which is exactly CLAUDE.md section 12's stop condition — a known failure
changed value.

**The fix is RETAINED.** It is correct about the time level, the gate measures that, and nothing
here contradicts it. **TD-032 is BLOCKED**, not failed, and the blocker is a question about a
different configuration than any criterion gates.

---

## 7. RESOLVED by the discriminating probe — logs/07 and logs/08

The probe named in section 5 was authorized and run on both libraries. **None of the three readings
in section 4 is correct.** The answer is a fourth thing, and it is measured rather than inferred.

### 7.1 What the field showed — `logs/07_window64_probe.log`

```text
                        status      S_L cm/s   front mm   crossings of 1050 K   wall
post-fix  window 32   Converged     4.67194     2.4747    1 at 2.4747 mm       330 s
post-fix  window 64   NoFlame       7.39035     3.5017    1 at 3.5017 mm        14 s
pre-fix   window 32   Converged     4.67194     2.8055    1 at 2.8055 mm       496 s
pre-fix   window 64   Converged     4.67194     2.8055    1 at 2.8055 mm       777 s
```

**This probe's own auto-verdict ("1 CROSSINGS EXIST yet NoFlame was reported -- a FALSE REPORT") is
WRONG,** and the error is worth recording because it is the fourth of its kind in this campaign. The
reading assumed the reported field was a marched state. The post-fix window-64 row finished in
**14 s** against 330 s, its upstream cells are at *exactly* 300.0000 K, its crossing sits *exactly*
on the anchor, and `S_L` is 7.39 rather than 4.67 — it is the **seeded profile**, because
`finalise()` reports whatever state exists and almost no marching happened. The crossing is the
seed's and says nothing about a flame.

### 7.2 What the per-trial record showed — `logs/08_why_noflame.log`

```text
driftWindow 32  -> Converged,  15 trials, bracketSteps 4
  trial 3   mdot 0.0870611   drift 0.207889   steps 40   measured YES   extinguished YES   sided
driftWindow 64  -> NoFlame,     4 trials, bracketSteps 0
  trial 3   mdot 0.0870611   drift 0          steps 40   measured NO    extinguished YES   UNSIDED
```

**Same trial, same mass flux, same 40 steps; only the window differs.** A drift needs *more* than
`driftWindow` steps of history before it can be read, so at 32 the window fills and the trial is
sided, and at 64 it cannot be and the search aborts during bracketing.

### 7.3 The three registered readings are all dead

```text
(c) the pre-fix pass was FALSE          dead -- pre-fix window 64 is a COLD-branch field with a
                                        genuine crossing at 2.8055 mm, identical to window 32
(b) the flame capability was LOST       dead -- post-fix window 32 finds the flame at 2.4747 mm
                                        with S_L 4.67194
    NoFlame is a false report           dead -- it is a TRUTHFUL report of an unsided trial under a
                                        MISLABELLED name
```

### 7.4 What it actually is

**A latent fragility in the search, which the fix moved the solver across rather than created.**
`driftWindow` must be smaller than the shortest trial, and **nothing enforces or checks that.** The
fix shortened trial 3's life to 40 steps, crossing a boundary that was always present. Pre-fix the
condition happened to hold; post-fix it does not.

Two defects follow, both separable from the time-level staging this item fixed, and both carved out
rather than repaired here:

```text
TD-036  `NoFlame` is mislabelled. At PremixedFlame.cpp:1746 and :1789 it means "a trial could not
        be sided"; the enum documents "the anchor temperature is never crossed: nothing is
        burning". `diagnostic` is EMPTY on this path, so the result carries no explanation at all.
TD-037  `driftWindow` has no guard against exceeding trial length. A trial that extinguishes in
        fewer steps than the window is silently unsidable and aborts the search with no diagnostic.
```

### 7.5 The remaining link, and the limit on what is claimed

`logs/09_why_noflame_prefix.log` measures the same per-trial record on the pre-fix library, to
establish whether trial 3 survived longer than 64 steps there. Until it lands, **"the fix shortened
trial lifetimes" is a sound inference and not a measurement**, and CLAUDE.md section 6 does not
permit attributing the change on an inference. What IS measured and attributed is the transition
itself (section 1) and the windowing mechanism (section 7.2).
