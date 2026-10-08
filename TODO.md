# CFDApp — TODO

> **This file owns:** current execution state — what is complete, partial or blocked, the current
> blocker, what comes next, and what is authorized. It answers *where is CFDApp now*.
>
> **It does not own:** direction and phase definitions → [ROADMAP.md](ROADMAP.md) · technical debt →
> [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) · measurements → `results/` · rules →
> [CLAUDE.md](CLAUDE.md).
>
> **This file is authoritative for live status**, and it is a snapshot of the tree, not a diary.
> Stale entries are corrected, not appended to; git and `results/` hold the history.

```text
[x] COMPLETE        implemented AND verified with evidence (CLAUDE.md §1.1)
[~] PARTIAL         implemented but not fully qualified; the reason is always stated
[ ] NOT AUTHORIZED  appearing here is not authorization (CLAUDE.md §2)
```

---

# 1. Project Status

```text
P0–P16   [x] COMPLETE     foundations through turbulence
P17      [~] PARTIAL      implementation complete; qualification partial (G6, G10)
P18      [~] PARTIAL      implementation substantially complete; G8 NOT CLOSED
P19–P28  [ ] NOT AUTHORIZED      definitions in ROADMAP.md §8–§17
```

**No implementation phase is authorized.** The active work is technical-debt resolution under a
separate authorization.

---

# 2. Current Blocker — P18 G8

G8 requires a 1D laminar premixed flame whose flame speed lies inside a cited published spread,
frozen *before* measurement at `results/p18/g8_closure/g8_reference.md`:

```text
35.0 – 37.0 cm/s      Hermanns (2007), DOI 10.6100/IR630126
```

**TD-032 is RESOLVED** (2026-10-07, gate only, TD-037 carved out). It was the root cause beneath
five other items: the march's transient term read a time level one step stale, so the reported field
alternated every step. The immediate blocker is now **TD-037**, which carries the G8 failure TD-032
exposed:

**TD-037 is also RESOLVED** (2026-10-07, gate only). The eigenvalue search now returns the same
answer at `driftWindow` 16, 32, 64 and 128 instead of aborting at two of them, and the G8
method-insensitivity test TD-032 broke **passes with its spread exactly 0**.

```text
TD-032, TD-037  both RESOLVED on their frozen gates, both with REGRESSION PARTIAL — Debug+GUI not
                run, sanitizers infeasible for this suite (TD-035). clang-format is CLEAN as of
                2026-10-09 (741 files, exit 0), so TD-034 is no longer a reason.
still open      TD-033, TD-035, TD-036, TD-038 — none authorized
TD-004          locator fix APPLIED and RETAINED: the front now sits 0.039 cells off the
                anchor (was 51-86 cells), zero locator rejections, and C2-A1's three clauses are
                SATISFIED. The item STOPS on a pre-registered condition -- S_L moved +6.07 % --
                because the working locator makes a non-conservative operator fire far more
                often. New blocker: TD-040.
re-measured     TD-028's alternation is measured GONE (status is a decision).
                TD-030 — the reaction excess is HALF its recorded size and no longer the larger
                term. The inlet diffusive flux (~5.5 %) is a flame-ANCHORING effect and has been
                MOVED to TD-004, whose blocker is now quantified: the flame settles ~1.73 mm
                (87 cells) upstream of its anchor and is still translating after 51 583 steps.
to re-measure   TD-027's C4, TD-029, TD-031 — still on the alternating field
next action     none authorized
```

**Consequence for everything downstream:** every field-derived premixed-flame number recorded
before 2026-10-06 was measured on a march whose reported field alternated every step, and must be
re-measured under its own item's gate. **TD-027 is REOPENED** on that basis — its C4 was invalidated
and its closure rested on it, though the cap value itself still stands on its other criteria.

**SUPERSEDED:** the earlier "`TD-004 → TD-005` order is inverted because the blocker is flame
stabilisation" question is on hold. That classification rested on the pre-fix front position.

---

# 3. P17 — Compressible Flow · PARTIAL

Implementation complete. Both outstanding gates are **inherited** debt, not new work.

```text
[ ] G6   HLLC star-density positivity fails for a characterized class of admissible head-on
         input. Clauses (a)(b)(c)(d)(f) pass; (e) fails.                → TD-008
[ ] G10  Debug+GUI has 4 timeouts; Release+CUDA and CPU-only both PASS.  → TD-009
```

Evidence: `results/p17/FINAL_QUALIFICATION.md` · limits: `results/p17/KNOWN_LIMITATIONS.md`

---

# 4. P18 — Species / Reactions · PARTIAL

| Sub-phase | Status | Gate |
| --- | --- | --- |
| P18.0 Baseline & audit | [x] COMPLETE | fingerprint and gates frozen |
| P18.1 Multi-species state | [x] COMPLETE | **G2 PASS** |
| P18.2 Species transport & diffusion | [x] COMPLETE | **G4 PASS** |
| P18.3 Mixture properties | [x] COMPLETE | **G5 PASS** |
| P18.4 Reaction source framework | [x] COMPLETE | mass-conserving sources |
| P18.5 Arrhenius chemistry | [x] COMPLETE | **G6 PASS** |
| P18.6 Heat-release coupling | [x] COMPLETE | **G7 PASS** |
| P18.7 Conservation & validation | [~] PARTIAL | **G8 NOT CLOSED** |
| P18.8 CPU / GPU equivalence | [x] COMPLETE | within the frozen P18.8 contract |
| P18.9 Final qualification | EXECUTED | **outcome PARTIAL** |

```text
G1 PASS · G2 PASS · G3 PASS · G4 PASS · G5 PASS · G6 PASS · G7 PASS
G8 NOT CLOSED · G9 PASS (scoped) · G10 PASS · G11 PASS
```

P18.9 ran in full; the **outcome** is partial because G8 is open. Elemental conservation is **N/A** —
elemental metadata is not modelled. The plug-flow benchmark does not replace G8.

Evidence: `results/p18/FINAL_QUALIFICATION.md` · limits: `results/p18/KNOWN_LIMITATIONS.md`

---

# 5. Required Work to Close P18

```text
1  re-measure the premixed-flame items           on the fixed march, each under its own gate
3  qualify the reacting-flow linear solver       TD-003
4  deterministic flame-speed extraction          TD-005
5  run G8's controls and close G8                TD-006
6  re-run P18.9, record a new fingerprint        TD-007
7  mark P18 COMPLETE only if G1–G11 PASS
```

**No P18 gate may be weakened to accomplish this.**

---

# 6. Technical Debt

The register is **[TECHNICAL_DEBT.md](TECHNICAL_DEBT.md)** — one row per item with root cause,
priority, dependencies, acceptance gate and evidence. The dependency model is a **graph**, not a
chain. The currently actionable frontier:

```text
resolved          TD-032   transient time level one step stale — RESOLVED 2026-10-07 (gate only)
resolved          TD-037   driftWindow unguarded against trial length — RESOLVED 2026-10-07
resolved          TD-034   clang-format clean — RESOLVED 2026-10-09 (741 files, exit 0)
current blocker   TD-040  recentring is not conservation-neutral; it carries TD-004's stop
to re-measure     TD-027, TD-028, TD-029, TD-030, TD-031 — all were measured on the alternating
                  field, each needs re-measuring under its own gate
also open         TD-033, TD-035, TD-036, TD-038, TD-039, TD-040 — none authorized
n/a as a defect   TD-041   CRLF growth — REFUTED 2026-10-09 by the CR/lines ratio: 1.000 on
                  both sides for all 21 files. CRLF is the committed convention
next              determined by TECHNICAL_DEBT.md; nothing is authorized
```

---

# 7. Future — Not Authorized

```text
P19  Multiphase                       P24  HPC & Distributed Scaling
P20  Advanced SST                     P25  Production Workflow & Reliability
P21  Advanced Numerics                P26  Verification & Validation Expansion
P22  3D Physics Parity                P27  Performance & Release Hardening
P23  Moving Mesh Production           P28  v1.0 Final Qualification
```

Definitions, goals, capabilities, dependencies and scope boundaries: **[ROADMAP.md](ROADMAP.md)
§8–§17**. Appearing there is **not** authorization. **Do not begin P19 merely because it appears in
the roadmap.**

---

# 8. Execution Order

```text
P0–P16 COMPLETE  ->  P17 PARTIAL  ->  P18 PARTIAL (G8 not closed)
   ->  the P18 debt work (§5, §6)
   ->  P18 requalification; COMPLETE only if G1–G11 PASS
   ->  P17's remaining qualification debt as priority requires
   ->  P19 … P28, each on explicit authorization
```

---

# 9. Rules

These restate [CLAUDE.md](CLAUDE.md), which is authoritative.

```text
 1  Audit before implementing.
 2  Freeze acceptance criteria before final measurements.
 3  Preserve the original failure before fixing a defect.
 4  Never weaken a valid threshold to obtain PASS.
 5  [x] means implemented AND verified.
 6  Preserve negative controls.
 7  CPU numerics remain the reference unless explicitly superseded.
 8  Conservation before performance.
 9  Numerical equivalence before speed claims.
10  Do not repair unrelated technical debt inside another phase.
11  Reuse existing infrastructure before creating a new subsystem.
12  Avoid duplicate transport, EOS, energy or chemistry implementations.
13  Reject invalid physical states explicitly; do not silently clamp.
14  Do not normalize conserved states merely to hide drift.
15  Implementation and qualification remain separate.
16  Every major phase gets a frozen baseline and a final fingerprint.
17  Every major phase records known limitations.
18  Every CPU/GPU phase records CUDA diagnostic scope.
19  Every performance comparison uses equivalent numerical solves.
20  Full regression follows production-code changes.
21  Detailed evidence belongs under results/.
22  Technical debt belongs in TECHNICAL_DEBT.md.
23  TODO.md holds current state, not historical logs.
24  No commit or push without authorization.
25  No future phase begins without explicit authorization.
```

Evidence layout: one `results/<phase>/` per qualified phase, one
`results/technical_debt/<item>/` per debt item. Future phase directories are created only when that
phase is authorized.

---

# 10. Current Action

```text
PROJECT STATE          P17 PARTIAL · P18 PARTIAL
BLOCKING ISSUE         P18 G8 — not closed; next is the re-measurement in §2
ROADMAP AUTHORIZATION  NO NEW FUTURE PHASE AUTHORIZED
PUSH BLOCKER           TD-035 — the §8 regression has NOT been run on this tree (ASan 38x).
                       clang-format is CLEAN and git diff --check is 0 (TD-034 resolved
                       2026-10-09); TD-041 was refuted, so neither blocks a commit
```

## Decisions waiting

```text
1  TD-040 next step   AUDITED: the mechanism is derived exactly -- each translation discards
                      fresh reactant upstream and fabricates burnt product downstream. Next is a
                      recentreDeadBandCells ladder (no production change) to test whether the
                      eigenvalue tracks the ledger rate. Authorize that?
2  the dt ladder      re-measure on the fixed tree to get a Richardson limit not inherited from
                      the blind-locator solver. Neither 4.671937 nor 4.955332 cm/s is verified.
2  TD-035             the full sanitizer regression CLAUDE.md §8 requires is infeasible for the
                      premixed-flame suite. Options: a stated-scope subset, a purpose-built cheap
                      fixture, or an amendment to §8 for suites of this cost. Raising the per-test
                      cap is what TD-009 forbids and is not proposed.
```

## Not authorized, and not to be started implicitly

TD-003 and every later debt item · every phase in §7 · any commit or push.
