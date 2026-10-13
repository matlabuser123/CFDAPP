# TD-032 — the march's transient term used a time level ONE STEP STALE

```text
STATUS:            TD-032 RESOLVED 2026-10-07, with TD-037 carved out, on explicit user
                   authorization. Closed on its FROZEN GATE, not on a complete CLAUDE.md §8
                   regression -- see section 5 for exactly what was not run.
SCOPE:             `src/species/PremixedFlame.cpp` time-level staging ONLY. Deliberately not done:
                   removing recentre()'s now-dead shifts of temperatureOld/compositionOld; any
                   re-measurement of TD-004/005/027/028/029/030/031.
GATE:              GATE.md  sha256 52bc82554574e7b76220082da0b7e0af0dda2c8c025184bf89a22fe9bd3ed5c0
RESULT:            C0 PASS · C1 PASS · C2 PASS · C3(a) PASS · C3(b) PASS · C4 PASS · C5 PASS
                   C6 PASS · C7 PASS · C8 PASS.  No frozen criterion failed.

FILES CHANGED:     src/species/PremixedFlame.cpp           (production, 1 file)
PRODUCTION FILES:  src/species/PremixedFlame.cpp   dac2992006a51cce... -> f9f554bf9e1fe1fd...
TEST FILES:        none -- all three relevant test sources hash-identical to the freeze
EVIDENCE:          results/technical_debt/TD032_lagged_time_level/

FOCUSED TESTS:     gate criteria C0-C8, logs/03 and logs/04
REGRESSION:        Release full ctest 2296 s, 12 failures (7 G8 + 5 pre-existing non-flame).
                   C6's three suites 331 / 32 / 343, all passing.
                   ASan+UBSan at CI settings, scoped: 528 tests, ZERO sanitizer diagnostics.
                   NOT RUN: Debug+GUI, and the full sanitizer suite (section 5).
SANITIZERS:        clean -- no AddressSanitizer error, no LeakSanitizer report under
                   detect_leaks=1, no UBSan runtime error.

KNOWN FAILURES:    4 SST channel tests + DriverSeegmiller.CorrectedMeshSequence -- long-standing
                   P20/TD-009 debt, 39-85 files each in results/, structurally unreachable from a
                   lambda inside solvePremixedFlame.
NEW FAILURES:      G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial
                   PASS (744 s, 759 s recorded) -> FAIL, attributed to this fix by a
                   two-library comparison. ROOT CAUSE MEASURED and carved out as TD-037:
                   driftWindow 64 cannot side a trial that extinguishes in 40 steps. The
                   failure is OPEN under TD-037 and is NOT repaired here.

GIT HEAD:          3bd32f9 (the premixed-flame work is untracked relative to HEAD)
GIT STATUS:        large pre-existing uncommitted tree; this item added 1 production file change
GIT DIFF --CHECK:  not run -- clang-format is NOT clean on this tree, see section 5

TODO UPDATED:      yes
ROADMAP UPDATED:   no -- nothing about the project's direction changed
COMMIT:            none -- not authorized
PUSH:              none -- not authorized
```

---

## 1. The defect

`advance()` copied the transient term's old time level at lines 1189-1191, **before** the state was
updated. From step 2 of any march onward the old level trailed the state by one step, so the
transient path connected step `k-1` to step `k+1` and the even and odd steps became two interleaved
sub-sequences coupled only through the operator coefficients. Two sub-sequences with no transient
coupling need not share a fixed point, and they did not: the reported field alternated **every
step** between the cold preheat zone and a nearly fully burnt state 1484 K away, and the reported
front between the anchor (3.535 mm) and the inlet (0.261 mm).

Three features made it survive review:

* the first step of every march is seeded `old = current`, so a one-step check passes;
* the same three-line block is **correct** for `densityOld`, which is consumed within the step by
  the `dt` controller — one assignment site serving two different requirements;
* the transient tests in `tests/unit/thermal/` and `tests/unit/transport/` hand an assembly an old
  level and check what it builds. The assembly was always correct. The defect was in which field
  the caller passed, which no assembly-level test can see.

Full audit: `audit.md`. Discovery and the 30 parity-predicted verdicts:
`../TD031_final_march_length/PARITY_FINDING.md`.

## 2. The fix

Capture `temperatureOld` and `compositionOld` at the **top** of `advance()`, where the step begins.
`densityOld` stays where it was, because start-of-step is what its consumer needs. No copy and no
arithmetic is added — two existing statements moved — so the expected cost change is zero.

**Not done, recorded:** the fix makes `recentre()`'s shifts of those two fields dead code. Removing
them is cleanup and is forbidden in this item.

## 3. What the gate established

```text
C1  R = across/within  5.608991e+03 -> 5.000271e-01   against 1.0     500 cells
C2  R                  5.192449e+02 -> 5.000359e-01   against 1.0     250 cells
C3(a) temperatureResidual 9.893696e-01 -> 1.840822e-05  against 1.0e-2
C3(b) resid(26)/resid(18)     1.000069 -> 0.179491      against 0.50
C4  maxCompositionSumDeviation      -> 3.945849e-07     against 2.0e-06
C5  S_L 4.671937 cm/s, 0.000 % move                     against 3.99 %
C6  331 / 32 / 343, all passing
C7  worst test 1283 s                                   against 7200 s
C8  the lagged library fails C1 and C2, hashes recorded
```

`R = 0.50003` at both meshes is worth more than a PASS: the gate derived `R ~ 0.5` as the value a
smooth sequence must give, because a two-step difference is the sum of two same-signed one-step
differences. Measured to four figures at two independent meshes — the sequence is now linear in the
step. And `within = 5.52e-02 K` is eight orders above the `1e-9 K` vacuity floor, so the ratio was
evaluated on its merits, not through the fallback.

A clean-first rebuild reproduced `libcfdcore.a` **bit for bit** (`a9b01668…`), so the verification
was not an incremental-build artefact.

## 4. THE BLOCKER

`G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial` was recorded PASS twice pre-fix
(744 s, 759 s) and now FAILS: at `driftWindow` 64 the solver returns
`PremixedFlameStatus::NoFlame`. A two-library comparison with both source forms hash-proved
**attributes the transition to this fix**. Windows 16 and 32 converge with `S_L` identical.

**What is established is causation, not loss of capability.** The pre-fix PASS was computed on the
alternating march, whose hot branch crosses `T_anchor` everywhere — so a flame was found whatever
the physical state. A third reading, which the measurement cannot discriminate, is that the fix
turned a *false* pass into a *truthful* failure. `attribution.md` sections 4 and 5 state both and
name the cheap probe that would separate them.

**The fix is retained.** It is correct about the time level and the gate measures that. Per
CLAUDE.md section 12 the item stops here, with the failure and its attribution preserved.

## 5. What is NOT done, and must not be read as done

```text
Debug + GUI regression        NOT RUN
full sanitizer suite          NOT RUN -- and NOT FEASIBLE as specified: ASan costs 38x on this
                              suite (303 s -> 11612 s measured), so G8's 4343 s becomes ~46 h and
                              at least three tests would exceed ctest's 7200 s cap and be recorded
                              as TIMEOUTs. Carved out as TD-035.
clang-format                  FAILS on this tree, in files this item did not touch
                              (apps/gui/tests/test_cli_gui_equivalence.cpp,
                              include/cfd/app/ProjectRunner.hpp). Carved out as TD-034.
every field-derived flame     TD-004 C2 (and its front-displacement premise), TD-005 C3/C6,
quantity                      TD-027 C4, TD-028, TD-029, TD-030, TD-031 -- all measured on the
                              alternating field, all to be re-measured under their own gates.
```

## 6. Carved out during this item

```text
TD-033  AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue asserts a precondition
        its own constants cannot satisfy: 0.11 > 0.15 is false, and it fails at 0 ms before any
        solve. Pre-existing and independent of this fix.
TD-034  clang-format is not clean on this tree, in files outside this item.
TD-035  ASan costs 38x on the premixed-flame suite, which makes the full sanitizer regression
        required by CLAUDE.md section 8 impossible as specified for G8.
```

## 7. Harness defects found in my own instruments, recorded

```text
ctest piped through `tail -40`      the disabled-test list is longer, so the aggregate pass/fail
                                    counts were truncated away. The failure LIST was recovered
                                    from ctest's own LastTestsFailed.log, which is authoritative,
                                    but the "N passed out of M" line is not in logs/05. Fix the
                                    pipe before the Debug and sanitizer passes.
the attribution's EXIT trap         re-restored an already-restored tree and printed
                                    "*** RESTORE FAILED". A false alarm: the swap script correctly
                                    refused to double-apply. Verified independently -- source
                                    f9f554bf..., library a9b01668..., capture at line 1019.
a chained background job            silently died between G8 and the regression, producing no
                                    output. Jobs that MUTATE the tree are now launched on
                                    notification rather than chained, because a double launch of
                                    a source-swapping script could corrupt the tree.
```

---

# 8. CLOSURE — 2026-10-07, with TD-037 carved out

**Authorization.** Explicit user decision: *"close TD-032 with TD-037 carved out."* Recorded because
the closure rests on it rather than on the item satisfying CLAUDE.md §8 unaided.

## 8.1 What closure rests on

```text
the frozen gate            all ten criteria PASS (section 3); GATE.md 52bc82554574e7b7...
the fix is correct         the transient term now reads the state the step starts from; the
                           alternation is gone at two independent meshes and R = 0.50003 matches
                           the value the gate DERIVED for a smooth sequence
attribution is two-library both source forms hash-proved, the pre-fix library byte-identical to
                           the frozen lagged one
the new failure is OWNED   root cause measured, not inferred: TD-037. A pre-existing, unstated and
                           unchecked coupling between driftWindow and trial lifetime. The boundary
                           is not this item's doing; being on the wrong side of it is.
```

This is the campaign's established pattern for an unmasked blocker — keep the fix, keep the
threshold, name the blocker's owner — and it is the same shape as TD-004's C2, where a correct fix
worsened a gate because the defect it removed had been masking a second one.

## 8.2 WHAT CLOSURE DOES **NOT** REST ON, stated so no later reader has to find out

```text
Debug + GUI regression     NOT RUN
full sanitizer suite       NOT RUN, and not feasible as specified -> TD-035 (ASan costs 38x on
                           this suite; at least three G8 tests would exceed ctest's 7200 s cap and
                           record as TIMEOUTs). Scoped run covered 2 of 12 G8 tests.
clang-format               FAILS on this tree, in files this item did not touch -> TD-034.
                           This blocks any commit (CLAUDE.md §11), so the fix is NOT committed.
exact ctest pass counts    lost to a `tail -40` in this item's own harness; the failure LIST came
                           from ctest's authoritative LastTestsFailed.log, but the aggregate is
                           not in logs/05. §7 records the harness fix needed.
```

**So this is `REGRESSION: PARTIAL`, not a §8 full regression.** CLAUDE.md §8 permits only the
failures a gate names, and the G8 PASS → FAIL is not named by any criterion. Closing anyway is a
deviation, authorized above, and recorded here rather than smoothed over.

## 8.3 What a reader must NOT conclude from this closure

```text
NOT validated   any premixed-flame number. Every field-derived quantity in TD-004, TD-005,
                TD-027 C4, TD-028, TD-029, TD-030 and TD-031 was measured on the alternating
                field and must be re-measured under its own gate.
NOT closed      the G8 method-insensitivity failure. It is OPEN under TD-037.
NOT established that the fix shortened trial lifetimes. That attribution needs the pre-fix
                per-trial record (`logs/09`), and with TD-037 carved out it is TD-037's evidence,
                not this item's.
NOT committed   nothing is committed or pushed; neither was ever authorized, and TD-034 blocks it.
```

## 8.4 What this item delivered

The reacting-flow march's transient term now reads the state each step actually starts from. Before
the fix the reported field alternated every step between the cold preheat zone and a nearly fully
burnt state 1484 K away, and **thirty historical good/bad verdicts across this campaign turn out to
have been predicted by the parity of a step count nobody was choosing deliberately.** The per-step
change now falls 5.6x over a longer march where previously it did not fall at all — which is the
first evidence that *march until the state stops changing* is a sound stopping rule for this solver.

That, and not the gate's PASS column, is the result worth carrying forward.
