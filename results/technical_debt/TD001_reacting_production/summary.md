# TD-001 — SUMMARY — **RESOLVED**

```text
item        TD-001  Reacting-species production reachability · P0 (blocks correctness/qualification)
origin      P18.2, restated P18.3 / P18.5 / P18.7; KNOWN_LIMITATIONS 1.4
gate        GATE.md      sha256 0749efbf1120db1fa6689a5b86be2e5045a20722413e6c2770e4605d5c7c319b
amendment   GATE_A1.md   sha256 53aee7afa7b38193fee1d045d43f35720d038fb8b0b6d7851809c0b9969f43b3
amendment   GATE_A2.md   sha256 6b1d97e541b555b94524d9e95b44161646b95c13ebaeacab9da5d540f2da42f2
verdict     every criterion PASSES; every control FAILS what it guards; 325/32/339 all met
git HEAD    3bd32f918656f471e72b5f6ac7cb6c2cbf75a274   (no commit, none authorized)
```

---

## 1. The defect, and what closes it

`grep -c 'setup.reacting' src/app/ProjectRunner.cpp` returned **0**. The parser read a `reacting`
block, `CaseBuilder` turned it into live `ReactionMechanism` / `ReactionKinetics` /
`MassFractionState` objects — and `ProjectRunner` never looked at them. A case declaring a mechanism
produced a complete result set with `"species": []` and no reaction quantity anywhere. A
**seven-phase** gap under CLAUDE.md §9, with everything up to the builder working.

That grep now returns **3**, and a user-authored case file drives the full chain through the real
CLI:

```text
case format -> parser -> builder -> solver dispatch -> results/export -> CLI
```

## 2. The dispatch is one operator-split time step, and that was forced by measurement

A steady pass was implemented first and has **no** source treatment that is both conservative and
bounded:

```text
cell-averaged source     Y_A = -0.297                       a different equation, not an approximation
per-cell Patankar        every Y_i >= 0, max|sum Y - 1| = 3.00e-01
per-cell fully explicit  conserves to 4.92e-07, Y_A = -0.297
```

The asymmetry is structural: implicit consumption self-limits while explicit production runs at the
initial rate, so `sum_i omega_i = 0` fails for the *effective* source though P18.4 guarantees it for
the source. Splitting removes both failures — the transport half carries no reaction (M-matrix,
bounded) and the chemistry half is P18.6's qualified reactor (conserves per cell, refuses rather
than clips).

Chemistry runs **first**, because `advanceClosedReactor` consumes a `MassFractionState` whose
constructor enforces P18.1's `4*eps*N` closure that no iteratively transported composition can meet.
Both orders are first-order Lie splittings, so the order was free to choose for that structural
reason. The two alternatives — renormalising the transported composition, or adding a raw-fields
reactor entry point — were rejected and why is recorded in `implementation.md` §3.4.

## 3. The headline measurements

Two predictions written down from the case file alone, before the case was run:

```text
k(T0) = 5.0 * exp(-30000/(8.314*1000))  = 0.13547969 1/s
dY    = k * Y_A,0 * dt                  = 1.354797e-03   measured 1.355075e-03   +0.021 %
dT    = dY * |h_f,B - h_f,A| / cp       = 2.709594 K     measured 2.710194 K     +0.022 %
```

```text
min_cell min_i Y_i        = +1.230741e-03     (>= 0, exactly, no tolerance)
max S_E                   =  2.710138e+04 W/m^3
max|sum Y - 1|            =  5.718198e-08     inherited, bounded and reported -- see section 5
C10 chemistry-off         =  BITWISE identical on all three cases, 9 hashes
```

## 4. Two gate criteria were wrong, and both were amended rather than worked around

This item stopped twice. Neither stop was repaired by weakening anything.

**C4 (original) — unachievable by construction.** It applied P18.1's `4*eps*N` bound for a
*constructed* composition to an *iteratively solved* field. Preserved verbatim in
`verification.md`; replaced by amendment **A1**.

**C4b (A1) — scaled the wrong variable, and C6/C7 sat below the noise floor.** A1 reasoned the
deviation was linear-solve error. A four-point tolerance ladder moved the residuals **430x** and the
deviation not at all. Root-caused three ways — it survives with chemistry off; one species at `Y≡1`
reproduces all of it; it scales with `dt` and with the flow's achieved pressure residual. A1 §2's
derivation had omitted that the convective operator annihilates a constant only for a discretely
divergence-free flux, which SIMPLE satisfies to its tolerance. Separately, control **N3 did not fail
C7**: `5.69e-05 K` of transport noise cleared a `1e-9 K` threshold, and with zero reaction enthalpy
the sign clause was undefined. Both preserved in `verification_A1.md`; replaced by amendment **A2**,
authorized by the user after the derivation was put in front of them.

A2's own pre-freeze dry-run then caught two **instrument** defects — a zero-prediction loophole that
would have excused controls A2-N2 and A2-N3 from the criteria they exist to break, and C4b-ii being
keyed to a flow tolerance the case never reaches. Both fixed before the freeze, both recorded in
`GATE_A2.md` §4.1. That is the dry-run doing exactly what §5.2 asks of it.

Every A2 replacement is **stricter** than what it replaced:

```text
C4b-i    production  ratio 8.0780 (/10 = 0.8078)  PASS      A2-N1 Patankar  ratio 1.0000  FAIL
C4b-ii   p_res fell 100.0156x  ->  D fell 83.9880x  PASS
C6b      predicted dY band, lower bound 13 000x the N2 noise floor
C7a/C7b  predicted dT band, lower bound 23 800x the N3 noise floor; sign defined for any mechanism
```

**N3 now fails C7 twice over while still passing C6b** — it removes the energy coupling without
removing chemistry, and only the energy criteria notice. That is the control gate §3 called one of
"the two that matter", working for the first time.

## 5. What this does NOT claim

* **`max|sum Y - 1| = 5.72e-08` is inherited, not eliminated.** It is the flow solver's residual
  continuity error carried into the composition, scaled by `dt/(rho V)`. It is now bounded, scaled
  and reported. It was **not** made to disappear by renormalising, clipping, or solving the carrier
  as `1 - sum` — each of which would have hidden it, and the first two are the silent normalisation
  TD-002's authorization forbids.
* **One step, not a march.** A second step's chemistry would need a raw-fields reactor entry point —
  **TD-002**. Recorded in `ProjectRunner.hpp` beside the scope statement.
* **TD-002's `sum Y = 1` step budget applies** to what this ships; the chemistry is P18's explicit
  first-order step.
* **No flame, no flame-speed extraction, no G8 closure** — TD-004 / TD-005 / TD-006.
  `PremixedFlame.{hpp,cpp}` is neither used nor validated here.
* **No performance claim.**
* **No GUI claim.** The gate does not require GUI for this item; `CLAUDE.md` §9's GUI leg is
  untested by TD-001 and the reacting fields reach the export pipeline the GUI reads, nothing more.

## 6. Files changed

**Production**

```text
src/app/ProjectRunner.cpp               the dispatch, and the reacting species' export metadata
include/cfd/app/ProjectRunner.hpp       ReactingRunResult, ProjectRunResult::reactingResult
src/io/CaseReader.cpp                   reacting species names reach the boundary parser
src/io/CaseBuilder.cpp                  timeStep and the per-patch per-species boundary fill
src/io/case/PhysicsConfigParser.cpp     reacting.time_step required/validated; thermal required
src/io/case/BoundaryConfigParser.cpp    the per-patch sum Y_i = 1 constraint (C11)
src/io/case/Parsers.hpp                 parseBoundaryConfig gained compositionSpecies
include/cfd/io/SimulationSetup.hpp      ReactingSetup::timeStep, ::compositionBoundaries
include/cfd/io/case/PhysicsConfig.hpp   ReactingPhysicsConfig::timeStep
```

**Tests** — `tests/unit/io/test_reacting_case.cpp` (fixture repaired, 3 tests added, 2 contaminated
instruments fixed).

**Case** — `cases/reacting_plug_flow/` (new: case/geometry/mesh/solver/boundaries/physics).

**Nothing under `src/species/` or `include/cfd/species/`.** The P18 library is reused, not modified,
which is what lets the 339 count stand as an untouched-library check.

## 7. Evidence

```text
audit.md                  root cause: grep -c 'setup.reacting' = 0; the carrier policy
reproducer.md             pre-fix failure modes V1/V2/V3
GATE.md                   frozen C1-C12, N1-N5, baselines 325/32/339
GATE_A1.md                amendment A1 -- C4 -> C4a..C4d
GATE_A2.md                amendment A2 -- C4b, C6, C7  (frozen 6b1d97e5, authorized)
proposed_amendment_A2.md  A2's derivation as it stood when authorization was requested
implementation.md         what was built, and why each decision was forced
verification.md           the ORIGINAL C4 failure, preserved verbatim
verification_A1.md        the C4b failure and N3's failure to fail C7, preserved verbatim
verification_A2.md        the authoritative run -- every criterion PASS
negative_controls.md      N1-N5, A1-N1/N2/N3 (pre-A2 run)
regression.md             328 / 32 / 339, the stale-binary stop, the contaminated instruments
data/                     production_fields.csv, production_metadata.json, c10 hashes
logs/00..24               freeze records, dry-runs, the authoritative rerun, the regression
```

## 8. Required final report

```text
STATUS:            TD-001 RESOLVED
SCOPE:             reacting-species production reachability through the real ProjectRunner/CLI path.
                   Deliberately NOT done: stiff chemistry (TD-002), the solver audit (TD-003),
                   flame/extraction/G8 (TD-004/5/6), any performance or GUI claim.
GATE:              GATE.md 0749efbf… + GATE_A1.md 53aee7af… + GATE_A2.md 6b1d97e5…
RESULT:            C1 C2 C3 C4a PASS · C4b-i 0.8078 PASS · C4b-ii 83.99x PASS · C4b-iii PASS
                   C4b-iv 5.718198e-08 reported · C5 PASS (grep 3, was 0) · C6a C6b PASS
                   C7a C7b PASS · C8 PASS · C9 PASS (6/6 refused, no partial builds)
                   C10 BITWISE · C11 PASS · C12 PASS.  No criterion failed.

FILES CHANGED:     9 production, 1 test, 1 new case directory (section 6)
PRODUCTION FILES:  section 6
TEST FILES:        tests/unit/io/test_reacting_case.cpp
EVIDENCE:          results/technical_debt/TD001_reacting_production/

FOCUSED TESTS:     G8ReactingCaseTest 12 run / 12 passed / 0 failed
REGRESSION:        CFDIoTests 328/328 (325 baseline + 3 enumerated) · CFDAppLayerTests 32/32
                   CFDSpeciesTests 339/339 under the baseline's own -G8PremixedFlameTest exclusion
                   Debug+GUI, sanitizers, clang-format suite, full ctest: NOT RUN (regression.md §9)
SANITIZERS:        NOT RUN (no commit authorized; TD-001 adds no new memory ownership)

KNOWN FAILURES:    G8PremixedFlameTest -- TD-004 territory, excluded from the 339 baseline by that
                   baseline's own record, "most currently FAIL", now 17 declared tests vs 13
                   recorded. Not measured or relied on by TD-001.
NEW FAILURES:      none

GIT HEAD:          3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
GIT STATUS:        TD-001's files plus a large pre-existing uncommitted P12-P18 body. Runtime noise
                   from the C10 runs restored; four cases/*/results/metadata.json left as found
                   (not this item's, regression.md §8).
GIT DIFF --CHECK:  clean apart from the three pre-existing CRLF files TD-018 names
                   (cuda/CMakeLists.txt, tests/unit/{compressible,gpu}/CMakeLists.txt)

TODO UPDATED:      yes
ROADMAP UPDATED:   no -- ROADMAP describes capability and limits; the reacting production path is a
                   capability change and the entry is proposed below rather than written unasked.

NEXT REQUIRED USER DECISION:
  (a) proceed to TD-002 (stiff / bounded chemistry integration, next in the dependency order and
      the item that unblocks a multi-step march), or
  (b) first update ROADMAP.md to record the reacting production path as capability gained with its
      stated limits, or
  (c) authorize a commit of TD-001, which would require CLAUDE.md §8's full ladder first
      (Debug+GUI, sanitizers at CI settings, clang-format) -- none of which has been run.
COMMIT:            none
PUSH:              none
```
