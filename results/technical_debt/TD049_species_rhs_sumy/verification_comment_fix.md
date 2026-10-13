# TD-049 — the stale production comment at `PremixedFlame.cpp:615-619` is CORRECTED. Objects BIT-IDENTICAL.

```text
STATUS:      **DONE.** TD-049's first concrete task, carried in `handover.md` §7, is closed.
             The comment asserted the cause that this item's own measurements refuted.
SCOPE:       a COMMENT ONLY. Five lines replaced by five lines. No statement, no constant, no
             threshold and no test changed. `relativeTolerance` is still `1.0e-11`, and the
             `1.0e-8` at `test_premixed_flame.cpp:471` still fails exactly as before.
HASH:        src/species/PremixedFlame.cpp
               was fa815c97216584485e1c94842669fbf71976b9fb06863096868972d88448e3a2
               now 19db4b6b3b5279055899d1051763b6f5ae85926aa1f4fe2d4d0c5038b405fe9d
             2006 lines before, 2006 lines after -- preserved deliberately (§2).
PROOF:       the compiled object is **BIT-IDENTICAL** at `-O3 -DNDEBUG -std=c++20`
             (`11985df3ab5cc1257b5ed6729e87c2bf6e3875aa5f82e3a8a0d43a2a468a39a2`), with both a
             determinism control and a perturbation control (§4). No computed value can differ,
             so no test run is needed to establish equivalence and none is claimed.
AUTHORIZED:  by the user, this action, 2026-10-12.
```

---

## 1. WHAT WAS WRONG

The comment justifying `linearSettings.relativeTolerance = 1.0e-11` read:

> "...the per-species systems must be solved tightly for `sum_i Y_i` to stay at 1: with unity Lewis
> number every species shares the same matrix, so the N systems sum to the mixture continuity
> equation, and **that cancellation only holds to the tolerance each one is solved to**.
> `maxCompositionSumDeviation` is what reports whether it held."

The bolded clause is **refuted hypothesis #1** of this item. Measured: the solve contributes **~1 %**
of the deviation, and an exact Thomas solve of the same assembled systems still misses the bound by
**230x**. So the comment named the wrong term as the limiter, at the exact site TD-049 owns.

**What the comment got RIGHT, and is kept:** the telescoping argument itself. The per-species
matrices are **bit-identical** at `Le = 1` (measured, 0.0 difference), so `sum_i (A y_i) =
A (sum_i y_i)` holds *exactly* — not merely to solver tolerance. The old text understated its own
argument and then blamed the solver for the gap.

## 2. THE DESIGN CONSTRAINT: FIVE LINES IN, FIVE LINES OUT

The replacement is the same length **by requirement, not by coincidence**. Counted before editing:

```text
PremixedFlame.cpp:NNN citations across the repository      70
of those, BELOW line 619 -- i.e. would SHIFT on any
  change to the line COUNT                                 60
where they live                                            mostly results/, which is frozen
                                                           evidence and cannot be edited to follow
```

Changing the line count would have silently falsified 60 citations, most of them in evidence that
§4 and §10 forbid rewriting. This is the same failure already recorded in this campaign: TD-047's
fix shifted `ProjectRunner.cpp:816` to `:828` and left a stale citation in the register.

Verified unmoved after the edit — eight spot-checks against what the evidence cites them as:

```text
:625   linearSettings.relativeTolerance = 1.0e-11           :1052  conductivityField = ...
:662   const Real residualAcceptanceFraction = 1.0e-9        :1157  the same-density comment
:686   scaled.absoluteTolerance =                           :1172  "An EXPLICIT zero source"
:718   maximumCellDiffusionNumber * cellSizeForDt^2         :1616  result.maxCompositionSumDeviation = 0.0
```

Each line is also within `ColumnLimit: 100` (widths 92, 91, 95, 96, 95), and
`clang-format-18 --dry-run --Werror` exits 0 on the file.

## 3. THE REPRODUCTION MECHANISM STILL WORKS — both halves checked

`tools/instrument_full.patch` is the preserved means of reproducing every TD-003 measurement. A
production edit could have broken it, so both directions were tested:

```text
A  patch -p0 against the OLD source (git show 0d30cf0:...)
     applies CLEANLY -- no offset, no fuzz, no reject
     produces 4482d819132f8199b8b6f88b89731832367e2717836d166b23f57abb485d66cd
     == the hash recorded in logs/10_gain_prediction.log
     ** so every TD-003 measurement remains reproducible, from git, unchanged **

B  patch -p0 against the NEW source
     applies CLEANLY -- no offset, no fuzz, no reject (the line count was preserved, so the
     hunk positions did not move)
     produces fcf377e1d1e1dbb6e2b7555ea392e88a55343cef147b7cb87e78a9acc9a7070f
     and a diff of the two instrumented files shows EXACTLY the five comment lines and nothing
     else -- confirmed by `diff -u`, one hunk, 5 removed / 5 added
```

The patch itself was **not regenerated**. Regenerating it would have silently rebased it onto the
new source and destroyed check A, which is the one that matters.

## 4. BIT-IDENTICAL OBJECT, WITH THE CONTROLS THAT MAKE THAT MEAN SOMETHING

The old and the new source were compiled with the *exact* command the release build uses, taken
from `build/release/compile_commands.json`, with only the `-o` path redirected:

```text
flags                       -O3 -DNDEBUG -std=c++20
old.o                       211840 bytes   11985df3ab5cc125...
new.o                       211840 bytes   11985df3ab5cc125...
                            BIT-IDENTICAL
```

An "identical" verdict from a comparison instrument is worthless until the instrument is shown to
be able to fail, so:

```text
C1  recompile the SAME source again        IDENTICAL  -> the toolchain is deterministic for this
                                                          translation unit, so the comparison is sound
C2  perturb ONE real constant used by the flame, relativeTolerance 1.0e-11 -> 1.1e-11
    (2 diff lines, asserted, so the control is not vacuous)
                                           DIFFERS, 7 bytes  -> the instrument is NOT blind
```

With C1 and C2 both behaving, the bit-identical result is a **proof of no behavioural change**,
stronger than any test run: there is no difference for a test to detect. Nothing was built into
`build/` and no build directory was written to or removed; objects went to the scratchpad.

## 5. WHAT THIS EDIT INVALIDATES, reported rather than silently fixed

The old source hash is referenced in **57 files**, all under `results/`, and in **no live
document** — `TECHNICAL_DEBT.md` and `TODO.md` contain it nowhere. Almost every reference is a
historical record of the form "production was `fa815c97…` when this was measured", which stays
true and needs nothing.

**Two are different, and are flagged rather than edited** (they belong to other items; §2 forbids
repairing them from inside this one, and §4 forbids rewriting evidence):

```text
results/technical_debt/METHOD_library_hash_gates.md:60
    prescribes, as METHOD for future work: "GATE ON the SOURCE hash.
    `src/species/PremixedFlame.cpp == fa815c97...` is the stable identity of the code under test".
    That hash is now stale. A future harness written to that rule would gate on the wrong value --
    a fail-open hazard, since it would either abort on a legitimate tree or be quietly updated.
    THE RULE IS STILL RIGHT; only its example hash moved. New value: 19db4b6b3b527905...

results/technical_debt/TD004_premixed_flame/tools/  (SIX scripts, enumerated not estimated)
    run_a3_dryrun.sh, run_a3_dryrun2.sh, run_a3_m5.sh, run_a3_verify.sh, run_c5_precision.sh and
    run_c6.sh each record POST_SRC=fa815c97... as an expected value. Re-running any of them on
    this tree would report a source mismatch. They are TD-004's frozen harnesses and are correct
    AS RECORDS of that run.
    (I first wrote "5 scripts" here from a by-eye read of a file list and omitted
    run_a3_dryrun2.sh. Corrected before commit by enumerating them with a grep.)
```

Neither is a defect introduced by the comment fix; both are the ordinary cost of a production hash
changing, and they are listed so the next reader is not surprised by them.

## 6. THE REPLACEMENT TEXT

```cpp
  // The relative tolerance stays tight, but the SOLVE is not what limits sum_i Y_i - 1. The
  // telescoping is real and measured -- at unity Lewis number the per-species matrices are
  // bit-identical, so sum_i (A y_i) = A (sum_i y_i) holds exactly -- yet the solve contributes
  // only ~1 %: an EXACT solve still misses the bound by 230x. Tightness is kept, not justified,
  // here; the dependence is non-monotonic. Reported by maxCompositionSumDeviation. See TD-049.
```

Note what it deliberately does **not** say. It does not claim the tolerance may be loosened. The
dependence on tolerance was measured **non-monotonic** (refutation #1), so loosening is unproven,
and the comment says tightness is *kept, not justified* — which is the honest state. Citing
`TD-049` in a production comment follows existing practice: 89 `TD-NNN` references already exist
across `src/` and `include/`.
