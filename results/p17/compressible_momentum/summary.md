# P17.4 — Compressible Momentum: summary

Phase: P17.4, the first P17 item with genuine implementation work.
Gate: `acceptance_gate.md`, sha256 `ded6feaa03b53e9fd65018491e57c9c318383798bf13fbabc4b910cf4fd6b673`,
frozen **before** any P17.4 code existed and **unchanged** throughout. Nothing was committed or
pushed; `HEAD` is still `3bd32f91` of 2026-09-21.

---

## 1. What was built

One new operator and one guarded call.

```text
assembleDilatationalStressContribution        the -(2/3) mu grad(div U) part of the Newtonian
                                              compressible viscous stress under the Stokes
                                              hypothesis. Composed from the EXISTING, already
                                              qualified discretization::divergence and
                                              discretization::gradient -- no new discretisation.
                                              RHS-only, so the matrix, the diagonal, the
                                              relaxation and the pressure-correction coupling
                                              are all untouched.
one call site                                 in assembleRelaxedCompressibleMomentumComponent,
                                              behind includeDilatationalStress = FALSE.
```

Before P17.4 the compressible viscous stress was `mu lap(U)` only:
`grad(div`, `dilatation`, `bulkViscosity`, `twoThirds`, `Stokes`, `divU` returned **0 hits**
across `include/` and `src/`. That was the gap P17.0's audit identified and this phase closed.

The delta, measured against the pre-P17.4 sources — **not** against `HEAD`, which is 2026-09-21
with 286 files uncommitted against it:

```text
include/cfd/compressible/DilatationalStress.hpp           +72  new file
src/compressible/DilatationalStress.cpp                   +65  new file
include/cfd/compressible/CompressibleRelaxedMomentum.hpp   +19 -1
src/compressible/CompressibleRelaxedMomentum.cpp           +11 -1
tests/unit/compressible/test_compressible_momentum.cpp    +744 -0      7 -> 22 TESTs
src/CMakeLists.txt                                          +1 -0     ONE line is P17.4's
src/physics/MomentumEquation.{cpp,hpp}                       0         UNTOUCHED, by SHA-256
```

Measured, non-comment lines: **48** in `DilatationalStress.cpp`, **14** declaration lines in its
header, **5** at the call site (a 4-line guarded block plus one include), and **1** parameter in
the relaxed-momentum header -- about **68 non-comment production lines**. The two new files are
137 lines including their comments, and the rest of the phase is tests and evidence.

## 2. Verdict against the frozen gate

```text
group                                                   verdict
---------------------------------------------------------------------------------
F   the conservative form, term by term                 PASS
D   the dilatational stress term                        PASS
C   conservation                                        PASS
M   continuity/momentum consistency                     PASS (structural, all 6)
L   low-Mach / incompressible reduction                 PASS
B   boundary momentum                                   PASS, with B1-partial and B2-partial
                                                        stated as structural, not claimed
V   invalid states                                      PASS
G   preservation                                        PASS, G8 NOT PRESENT (not "PASS")
N   non-vacuity controls                                ALL DETECTED
```

Key measurements — the full set with derivations is in `verification.md`:

```text
the operator vs its analytical value, deep interior      worst error 0, EXACT
mu-scaling at a deep-interior cell                       exact at mu = 0, 0.05, 0.1, 0.2
a divergence-free field                                  bitwise 0 in all 100 cells
the wired path delivers exactly the operator             worst mismatch 1.44e-15 = 24 eps
the default is the disabled branch                       bitwise, all 100 cells
C2 uniform-flow residual                                 1.78e-15 vs a bound of 1.25e-12
C4 global conservation, per-cell O(1)                    sum 1.78e-15, 1.365e15 x cancelled
P13-001 SIMPLE fingerprint                               BITWISE IDENTICAL, empty diff
```

## 3. Regression

```text
                  registered  predicted  executed  predicted  failed  runtime
------------------------------------------------------------------------------
Release + CUDA        2608       2608      2546      2546        0    39.7 min
CPU-only              2406       2406      2346      2346        0    29.8 min
Debug + GUI           not run this phase -- deferred to P17.11 by standing decision
```

Every predicted figure matched exactly, both `COUNT VERDICT` lines read `MATCHES the frozen
prediction`, and the closing arithmetic balances in both trees:
`2608 - 13 excluded - 49 disabled = 2546` and `2406 - 13 - 47 = 2346`. The 15 P17.4 rows were
enumerated **by name** out of each ctest log and all 15 passed in both configurations, so they were
executed and not merely registered. `DilatationalStress.cpp` is compiled in the CPU-only tree
(1 hit, with nvcc and libcudart at 0), which is what closes the
`if(NOT CFDAPP_ENABLE_CUDA)` blind spot.

The freeze predicted **+9** tests; **+15** were implemented, so the prediction was revised to
`+15` *before* the run with the frozen figure kept beside it. Six extra tests, each traceable
to a frozen criterion that was **measured by nothing** — see §4. A test count is an arithmetic
consequence of the suite, not a tolerance; no threshold, tolerance, iteration requirement or
negative control was changed.

## 4. The real story of this phase: SEVEN frozen criteria were measured by nothing

More was found by auditing the gate than by writing the operator. The operator itself was
straightforward — two existing operators composed, verified exactly against an analytical field
on the first attempt. What took the work was discovering that the **gate did not measure what it
said**.

```text
criterion  what the gate said                    what was actually there
-----------------------------------------------------------------------------------------------
D3   "enabled, it CHANGES the answer"            NO TEST. Every dilatational test called the
                                                 operator DIRECTLY; nothing exercised the wired
                                                 path, so a wrong, absent or inverted wiring
                                                 would have passed.
D1   "disabled by default, bitwise identical"    probed by `grep -c` on a header STRING, which
                                                 is not a property of the assembly.
C2   "uniform flow residual at round-off"        NO TEST.
C4   "the integral momentum balance closes"      NO TEST.
V3   "NumericalError on a non-finite system"     NO TEST.
B3   "Symmetry contributes zero normal flux"     NO momentum test used the Symmetry BC at all.
L5   "assembleDiffusionContribution untouched"   probed by `git diff --numstat HEAD`, which
                                                 reported the file TOUCHED although P17.4 never
                                                 opened it -- 286 files are uncommitted against
                                                 a 2026-09-21 HEAD, so the probe measured every
                                                 earlier phase too.
```

All seven are now measured, each with a control, and every criterion text is **unchanged**. The
amendments record each one: `AMENDMENT_N2_N6_vacuous.md`, `AMENDMENT_C2_C4_unimplemented.md`,
`AMENDMENT_L5_instrument.md`.

### How they were found, which matters more than that they were

```text
D3, D1   by a negative control coming back VACUOUS -- it had nothing to break
C2, C4   by reading the frozen gate line by line against the list of tests that run
V3, B3   by the same reading
L5       by the probe raising a FALSE ALARM that had to be diagnosed
```

Only two of the seven surfaced through the control mechanism. The rest needed someone to check
the gate against the suite, which no automated step does.

## 5. Corrections to my own earlier claims

Recorded rather than edited away, because each was stated as fact and was wrong.

```text
1  "the MMS would fail if the term were enabled by default"
   MEASURED FALSE, twice. Under both N6 and N6b the MMS and low-Mach suites were 10/10 with the
   term on. The MMS order band is [0.55, 1.30] at nx <= 32 and is simply not sensitive enough.
   WITHDRAWN as a justification for default-off; the other two reasons stand.

2  "div U is exact EVERYWHERE"
   MEASURED FALSE. Exact in the interior, off by up to 10 in the boundary-adjacent layer. The
   original figure had been measured over interior cells only and the claim generalised it. It
   also contradicted the paragraph directly beneath it in the same document.

3  "N6 was vacuous because no test could detect the default"
   WRONG IN ITS DIAGNOSIS. A PRE-EXISTING test,
   CompressibleRelaxedMomentumTest.AlphaOneMatchesUnrelaxedCompressibleMomentumComponentExactly,
   does detect it -- but N6's fail-regex excluded the file it lives in, so the control never ran
   it. N6 was a TOO-NARROW PROBE, not a coverage gap. The genuine D3 gap survives this
   correction; the framing did not.

4  "gate attempt 3's post-edit output cannot be trusted"
   OVERSTATED. Its log is coherent end to end. The damage was elsewhere and worse: bash resumed
   from a shifted byte offset and began executing the PATCHED script as top-level commands,
   producing an uncontrolled partial second pass whose output bypassed tee. Established with a
   purpose-built probe rather than argued.
```

## 6. Process failures in this phase, and what they cost

```text
gate attempt 1   two tests FAILED: "no deep-interior cells" -- on an 8x4 mesh with h = 1,
                 y > 2 AND y < 2 is impossible. My own ASSERT_GT(checked, 0) guard caught two
                 vacuous tests. Fixed the MESH, not the criterion.
gate attempt 2   green, but on the defective L5 probe and an incomplete suite.
gate attempt 3   G6 clang-format FAILED (exit 74) -- and I then edited run_gate.sh WHILE IT WAS
                 RUNNING, because a "dry run" whose redirection silently failed became a live
                 run. Preserved in logs/PRESERVED_gate_attempt3_edited_midrun/.
gate attempt 4   fully green on the 19-test suite: P14 68/68, P15 161/161, P16 315/315.
gate attempt 4b  after C2/C4 were added: momentum 22/22, compressible 175/175.
gate attempt 4c  after B3 was added: momentum 23/23, compressible 176/176, format clean.
```

**Five of this phase's errors had one root cause**: composing a shell or Python command inline
instead of writing it to a file and running the file. `$PATH` expanded into Windows paths
containing parentheses; `$?` and `:(exclude)` pathspecs were mangled; a `pkill -f` pattern
matched its own parent shell and killed it; a heredoc backslash-processed `\n` into real newlines
and silently broke a string replacement twice. The rule is written down in
`logs/PRESERVED_gate_attempt3_edited_midrun/WHY_INVALID.md` and was followed for the rest of the
phase.

## 7. What is NOT done, and is not claimed

```text
NOT DONE       a GPU compressible momentum path. None exists to extend: G8 probes cuda/ and
               include/cfd/gpu/ for compressibleMomentum, CompressibleRelaxed, dilatational and
               DilatationalStress and finds 0 hits on all four. Reported [~] and NOT PRESENT,
               never PASS. No path was invented to satisfy the checkbox.
NOT DONE       case-file reachability. includeDilatationalStress is reachable from the library
               but has NO case-format key -- the same status pseudoTimeStep already has. A
               feature no case file can reach is not shipped capability.
NOT DONE       the full tensor viscous stress. mu[grad U + grad U^T] is not assembled as a
               tensor; the Laplacian plus this dilatational term is what exists.
NOT DONE       re-deriving the MMS forcing to include the new term. This is why the default is
               off, and it carries its own evidence burden.
NOT DONE       compressible body-force physics. The API contract is verified; no gravity or
               buoyancy exists on the compressible path and none was added.
NOT VERIFIED   accuracy within two cells of a boundary -- measured WRONG there, deliberately
               not claimed.
NOT VERIFIED   an order-of-accuracy study for the dilatational term. The convective scheme is
               first-order upwind and the system MMS is not sensitive to the term.
NOT VERIFIED   3D. requireTwoDimensional rejects it at entry; every claim here is 2D.
NOT VERIFIED   that a wall transmits pressure and viscous shear (B1, second half), and the
               per-face sign of inlet/outlet convective momentum (B2). Both are argued
               structurally and isolated by no test.
NOT VERIFIED   the converged-solution integral momentum balance. C4 measures the discrete
               property that makes it possible -- the operator creates nothing in the interior --
               which is narrower than the frozen sentence and is reported as narrower.
```

## 8. Standing debt carried forward, not introduced here

```text
the dual-cp hazard      compressible.specific_heat_pressure and thermal.specific_heat can
                        disagree. TODO.md says "decide before P17.5", and P17.5 is the first
                        consumer of cp. This is now the immediate blocker.
P13 never ran clang-format   0 mentions across its final qualification evidence tree, while 22
                        files currently fail clang-format-18. P17.4's own files are clean.
pseudoTimeStep          no case-format key, like includeDilatationalStress.
InternalFaceUsesArithmeticMeanDensity   non-discriminating.
value-validation asymmetry between the compressible and shared continuity evaluators.
```

## 8b. Repository integrity, after everything

```text
HEAD                                    3bd32f91, UNCHANGED from the freeze
branch                                  main
stash entries                           0
nothing committed, pushed, reset or stashed
src/physics/MomentumEquation.cpp         hashes to the freeze
include/cfd/physics/MomentumEquation.hpp hashes to the freeze
surviving control markers               0 across include/ src/ cuda/
surviving L5b probe marker              0
clang-format on all 5 changed files     exit 0
P17_CODE_FINGERPRINT during the run     4e253ce4... before == after  (B8)
P17.4 untracked files                   2, both the new DilatationalStress pair
```

The fingerprint **did** move from P17.3's `437ac0e3...` to `4e253ce4...`, which is correct and not
a finding: P17.4 changed code, and the fingerprint is a state identifier. Its pass/fail use is that
it must not move *during* a regression run, and it did not.

Two small defects in my own integrity check were found by reading its output and are fixed:
it listed all 124 untracked files under a heading claiming P17.4 introduced them (the same
mis-attribution the L5 probe made -- 122 belong to earlier uncommitted phases), and it hard-coded
`+14` for a suite size that is now derived. Three `*.pristine` control scratch files from the first
controls run are kept deliberately rather than tidied away.

## 9. Evidence index

```text
audit.md                             the pre-implementation audit, frozen
acceptance_gate.md                   the frozen gate, sha256 ded6feaa..., UNCHANGED
formulation.md                       the discrete equation term by term, with corrections
implementation.md                    what was built, and the proven delta
verification.md                      measured values, tolerances and their derivation
regression.md                        the execution policy, predictions and result
AMENDMENT_N2_N6_vacuous.md           controls N2/N6 vacuous, + the correction to my diagnosis
AMENDMENT_C2_C4_unimplemented.md     two conservation criteria measured by nothing
AMENDMENT_L5_instrument.md           the defective vs-HEAD probe, and its replacement
logs/PRESERVED_vacuous_deep_interior/        gate attempt 1's two failing tests
logs/PRESERVED_vacuous_N2_N6/                the first-pass vacuous controls
logs/PRESERVED_gate_attempt3_edited_midrun/  the script edited mid-run, and the reparse probe
```

## 10. The single recommended next step

**P17.5 — Total-Energy Equation**, and its gate freeze must resolve the **dual-`cp` hazard as
item zero.**

```text
why P17.5   TODO.md calls it "the main new physics requirement", and P17.4 completes the
            momentum half of the compressible system. Continuity (P17.3), the EOS (P17.2),
            variable density (P17.1) and momentum (P17.4) are now all verified; energy is the
            remaining conservation law before any density-based solver (P17.8) or Riemann flux
            (P17.7) has a complete state to work with.
why cp first  compressible.specific_heat_pressure and thermal.specific_heat can disagree, and
            the energy equation is the FIRST consumer of cp. Deciding it afterwards would mean
            re-deriving the energy gate. TODO.md already records "decide before P17.5".
```

Not started. No P17.5 file was created and no energy code was written.

### One process change I would carry into P17.5

Freeze the gate as now, then add one step before implementation: **map every criterion onto the
test that will measure it, and dry-run each probe against the unchanged baseline.** Seven of
P17.4's criteria were unmeasured or mis-measured, and five of those were found by hand, late.
That map costs an hour and would have caught all seven at the freeze.
