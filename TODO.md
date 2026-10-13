# CFDApp — TODO

**Operational tracker.** What is being done now, what is stuck, what is next. Debt detail is
[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md); direction is [ROADMAP.md](ROADMAP.md); rules are
[CLAUDE.md](CLAUDE.md); measurements are under `results/`. Pre-rewrite copy preserved verbatim at
`results/doc-archive/TODO-2026-10-12-pre-rewrite.md`.

## Current Status

- **Version:** pre-v1.0. P0–P16 COMPLETE · P17 PARTIAL · P18 PARTIAL · P19–P28 NOT AUTHORIZED.
- **Active Phase:** none. No implementation phase is authorized. The active work is the
  **technical-debt resolution campaign**, one item at a time, each separately authorized.
- **Last Verified:** 2026-10-12 — clang-format clean (742 files, exit 0); §8 regression ladder
  4 of 4 on this tree (Release 2844/6 known/6908 s · Debug+GUI 2906/18 · sanitizers 2832/14 with
  **0 sanitizer diagnostics** · format 742/0); register re-parsed at 49 items, 38 unresolved.
- **Next Priority:** a **user decision**, not a task — see *Decisions Required*. Nothing is
  authorized to start.
- **Git:** branch `td-campaign-evidence`, ahead of `main`, **nothing pushed** (push blocked below).

## Active Tasks

No task is currently authorized. These are the actionable items in dependency order; each needs
its own authorization before work starts (CLAUDE.md §2).

- [ ] **TD-049 — Species RHS `ΣY` closure:** explain why `max_c |Σ_i Y_i − 1|` parks at ~1e-6.
      *Deliverable:* a measured mechanism, plus a §5.3-derived threshold or proof the existing one
      is met. *Verification:* the mechanism must predict, not fit; 7 hypotheses are already refuted
      and none may be re-attempted without naming what is new.
- [ ] **TD-027 C3 — amend C3's instrument, then size the cap.** AUDITED 2026-10-12: C3's
      numerator and denominator are both set by `settings.tolerance`, so the ratio only takes
      `m × (q/B)` with `q/B ∈ (1,2]`; the threshold 2 is unreachable as a boundary and the
      criterion scores best (0) on a ladder that resolved nothing. *Deliverable:* a §5.3 amendment
      measuring the temporal error at a research tolerance while judging the bracket the shipped
      configuration publishes — **authorized; dry-run DONE 2026-10-13 (9.5 h), see `dry_run_A4.md`.
      **GATE_A4 FROZEN 2026-10-13 (`33dc8c94…`); fresh §5.3 step-6 run IN PROGRESS (~9.5 h).**
      *Verification:* the replacement must still reject cap 20 at 80× and cap 10 at 8.4× while
      returning a resolved value at cap 5. The threshold 2 does **not** move.
- [ ] **TD-004 C6a-ord — amendment A7 AUTHORIZED 2026-10-13**, instrument built and compiling;
      dry-run waits on GATE_A4's machine time. Substitutes the resolution UNIT (the quantum is
      the terminal bisection bracket at the probe's own tolerance, not a hardcoded
      `2^(1/1024)-1`); floor, band, formula and triplet all UNCHANGED. Predicted to turn
      UNRESOLVED into a definite FAIL at `[2.593, 2.801]`. ~1.3–1.8 h.
      *Will not settle* whether 125 cells is asymptotic. See `amendment_A7_proposal.md`.
- [ ] **TD-004 C6a — the observed-order clause:** reports UNRESOLVED, 2.00 quanta against a derived
      4-quantum floor. *Deliverable:* a refinement triplet that resolves the order, or a recorded
      finding that this fixture cannot. *Verification:* TD-043's 0.0677 % lattice bounds it.
- [ ] **TD-003 C2 — wire solver escalation into the reacting path:** `LinearSolverFallback` exists
      and is wired into SIMPLE/CompressibleSIMPLE but not `ProjectRunner.cpp:828`.
      *Deliverable:* escalation on `Breakdown`. *Verification:* existing results bitwise unchanged,
      since no measured matrix is in the failing regime.
- [ ] **TD-005 C6 — deterministic flame-speed extraction:** blocked behind TD-049's bound.

## Blocked

- [ ] **TD-006 — close P18 G8:** 8 of 11 declared dependencies open, 3 of them P0.
      *Required action:* close TD-049, TD-027, TD-004, TD-005 and TD-020 (G8's negative control 1
      cannot run without it). The precision precondition is **discharged** (method reproducibility
      4.17 % → 0.97 % against a ±2.78 % band); preconditions 2 and 3 — `cp`/`λ(T)` cited, and
      Westbrook & Dryer Set 3 wired to a case — are **not met**.
- [ ] **TD-007 — re-run P18.9 and record a new fingerprint:** blocked on TD-006.
- [ ] **P18 COMPLETE:** blocked on TD-007. Only G8 is open; G1–G7 and G9–G11 PASS.
- [ ] **P17 remaining qualification:** G6 → TD-008 (HLLC positivity), G10 → TD-009 (Debug+GUI
      timeout contract). Both **inherited** debt, not new work.
- [ ] **Any push:** blocked by TD-018 (102 tracked text files carry 21 621 CRLF lines) and by
      TD-035's unresolved judgement on the recorded cost limits. Neither blocks a commit.

## Next

- [ ] Re-measure every premixed-flame quantity recorded before 2026-10-06 — they were measured on a
      march whose reported field alternated every step (TD-032's root cause, now fixed). Each needs
      its own gate: TD-027 C4, TD-029, TD-031.
- [ ] P18 requalification; mark COMPLETE only if G1–G11 all PASS.
- [ ] P17's remaining qualification debt, as priority requires.
- [ ] P19…P28 — definitions in [ROADMAP.md](ROADMAP.md). Appearing there is **not** authorization.

## Decisions Required

These block progress and are not mine to make.

```text
1  TD-027 C3        A4 DRY-RUN DONE. Freeze GATE_A4 and run the fresh post-freeze verification
                    (~9.5 h)? The instrument works and is non-vacuous; C3-A4 still FAILS at
                    17.988 production / 2.248 fixture, as predicted. SEPARATELY: the shipped
                    tolerance default was FIXED 2026-10-13 (1e-4 -> 1e-3) under GATE_TOL, all 5
                    criteria PASS; coherence improved 8x to 2.248, still a FAIL, and NOT tuned
                    to pass. Correction recorded: 1e-4 was DEAD-WRITTEN, not "shipped".
                    (earlier) AUDITED: the criterion cannot express its own threshold.
                    Authorize the derived §5.3 instrument amendment? It needs a design choice:
                    the finest ladder level would breach ctest --timeout 7200 at the required
                    tolerance, so fewer levels, a coarser starting dt, or a DISABLED_ run.
                    SEPARATELY: the shipped default tolerance 1e-4 reads 40.63x, and the
                    production comment justifying the cap argues coherence at 1e-3 instead.
2  TD-004 C6a       order UNRESOLVED on this fixture. Accept the limit, or build a fixture
                    that can resolve it against TD-043's lattice?
3  TD-049 threshold a §5.3 amendment is DUE and NOT AUTHORIZED. It would REPLACE an underived
                    constant, not correct a stale one; the first attempt failed its own dry-run.
4  TD-035           the §8 sanitizer ladder carries 10 ASan and 14 Debug cost-driven timeouts
                    whose Release durations exceed 7200 s once slowed. The cap was NOT raised.
                    Acceptable before a push, or amend §8 for suites of this cost?
5  TD-003 / TD-004  does C2's unwired fallback INDEPENDENTLY block TD-004's completion? Not
                    established — the fallback never fired on any measured configuration.
6  TD-040           authorize a recentreDeadBandCells ladder (no production change)?
7  reacting tests   no automated test drives the reacting path end to end: 22 files call
                    ProjectRunner::run and none mentions reacting. Carve out as an item?
```

## Completion Gate

Per item, before it may be called resolved:

- [ ] Gate frozen before the change; dry-run on the unchanged baseline; non-vacuity shown.
- [ ] Focused tests pass for the code actually changed.
- [ ] Required regression passes (§8), or its scope limit is recorded and accepted.
- [ ] Evidence recorded under `results/technical_debt/<item>/`.
- [ ] [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) and this file updated to match the evidence.
- [ ] No threshold weakened; original failure preserved verbatim.

## Not Authorized

Every debt item, including TD-049 · every phase P19–P28 · **any commit or push**, each time.
