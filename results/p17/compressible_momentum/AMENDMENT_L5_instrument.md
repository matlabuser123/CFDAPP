# P17.4 amendment — the L5 "untouched" instrument was defective

Raised: 2026-09-27, during gate attempt 3.
Status: criterion **L5 UPHELD and PASSING**; the *probe* that measured it is replaced.

---

## 1. What the gate printed

```text
### L5: physics::MomentumEquation must be UNTOUCHED ###
  delta: '14	1	include/cfd/physics/MomentumEquation.hpp 13	2	src/physics/MomentumEquation.cpp '  (empty = unchanged)
```

Read literally, that says P17.4 modified a file the freeze forbade it to touch — a Prime
Directive violation. It is not what happened. The **instrument** is wrong.

## 2. The defect

`tools/run_gate.sh:29` measured the criterion as

```bash
git diff --numstat HEAD -- src/physics/MomentumEquation.cpp include/cfd/physics/MomentumEquation.hpp
```

`HEAD` is `3bd32f91` — `feat(gpu): complete resident CUDA SIMPLE pipeline`, **2026-09-21**.
**286 files are uncommitted against it.** So `git diff HEAD` reports the union of every
uncommitted phase's work, not this phase's. The delta it printed is P16's wall-function
change:

```diff
+                                   // P16 -- wall functions.
+                                   // Per-FACE effective viscosity, consulted only at BOUNDARY
+                                   const cfd::fields::SurfaceField* wallViscosity = nullptr);
+      if (wallViscosity != nullptr && (*wallViscosity)[faceId] != 0.0) {
+        muFace = (*wallViscosity)[faceId];
```

Not one line of it is P17.4's. The probe asks "does this file differ from the last commit",
and the criterion asks "did **this phase** change this file". Those are the same question
only for a file no earlier uncommitted phase touched.

## 3. Why this is a gate-derivation failure, not a detail

The probe was **never dry-run against the unchanged baseline**. Had it been, it would have
printed exactly the same non-empty delta *before P17.4 wrote a single line* — which is the
definition of an instrument that cannot distinguish the condition it claims to detect. It
does not fail closed and it does not fail open; it fails **indiscriminately**, and a reader
cannot tell a real violation from this artefact. That is the error my own recorded lesson
names: *derive each criterion from the quantity it measures, and dry-run it at baseline.*

## 4. The criterion is satisfied — proven by the right instrument

The freeze recorded the hashes for precisely this purpose (`logs/00_freeze.log`,
`### NOT to be touched ###`). Measured 2026-09-27:

```text
                                            frozen at 21:13:40Z            now
src/physics/MomentumEquation.cpp            e46d49fcda78e027…6dc7bb59      IDENTICAL
include/cfd/physics/MomentumEquation.hpp    7feca5c4a3a22943…bb0e37af      IDENTICAL
```

`physics::MomentumEquation` — and therefore `physics::assembleDiffusionContribution`, the
function L5 names — is **byte-for-byte untouched**. L5 **PASSES**.

## 5. The replacement, and its non-vacuity

The probe becomes a SHA-256 comparison against the frozen values. The original probe was
disqualified because it reports "touched" when nothing was touched; the replacement must
pass the test the original failed — it must be silent at the untouched baseline **and** must
fire when the file really is touched. Both are demonstrated in `logs/09_L5_instrument.log`:

```text
control L5a  file untouched                  -> UNTOUCHED   (the original printed "touched")
control L5b  one comment character appended  -> *** TOUCHED *** and restored by SHA-256
```

A probe that only ever prints PASS would be vacuous, so L5b is mandatory, not decorative.

## 6. The same defect corrupted the delta table — corrected

The `### the P17.4 delta ###` rows used the same `git diff --numstat HEAD`. Re-derived
against the pre-P17.4 sources, which were **reconstructed and then proven by the frozen
SHA-256** (`tools/reconstruct_pre_p17_4.py`):

```text
file                                              vs HEAD      TRUE P17.4     verdict
------------------------------------------------------------------------------------------
include/cfd/compressible/DilatationalStress.hpp   untracked    +72  new file   n/a
src/compressible/DilatationalStress.cpp           untracked    +65  new file   n/a
include/cfd/compressible/CompressibleRelaxedMomentum.hpp  +19 -1   +19 -1      agrees
src/compressible/CompressibleRelaxedMomentum.cpp          +11 -1   +11 -1      agrees
tests/unit/compressible/test_compressible_momentum.cpp   +376 -0  +376 -0      agrees
src/CMakeLists.txt                                        +32 -0   +1  -0      WRONG
src/physics/MomentumEquation.{hpp,cpp}              +14-1/+13-2   0 (sha256)    WRONG
```

Reconstruction proof — the recovered pre-P17.4 files hash to the frozen values exactly:

```text
5d97510fbac1f38c2dd0b5051a3cdccd22b3d960e9b3187e9a14444cc22a0cc5  CompressibleRelaxedMomentum.hpp
f44c8da4175f228f2b67bc225728e621d00534b2acc3f34cb028bccccbf57a4e  CompressibleRelaxedMomentum.cpp
```

Four of six file rows were right and **two were wrong**, and the two wrong ones are exactly
the files earlier uncommitted phases had also touched. `src/CMakeLists.txt`: of the 32 added
lines, **1** is P17.4's (`compressible/DilatationalStress.cpp`); the other 31 are P13-001
restart/checkpointing, thermal, P16 wall functions and GPU turbulence.

The test file's pre-P17.4 state is independently corroborated: `git show HEAD:` on it
contains **7** `TEST(` — the count the freeze recorded — so that file was clean vs HEAD and
its `+376 -0` is genuine.

## 7. Blast radius — checked, not assumed

Every `git diff` in the P17 tool tree was inspected.

```text
tools/*/code_fingerprint.sh, p17_*_regress.sh
    git diff --binary HEAD | sha256sum          LEGITIMATE. A whole-tree state IDENTIFIER
                                                compared only against a previously recorded
                                                value of the same quantity, so the constant
                                                HEAD offset cancels. This is the documented
                                                P17_CODE_FINGERPRINT and it is unaffected.
compressible_continuity/tools/run_gate.sh:28
    git diff --numstat HEAD -- physics/ContinuityEquation.*    SAME DEFECTIVE PATTERN.
                                                Re-measured 2026-09-27: genuinely empty --
                                                no uncommitted phase had touched those two
                                                files. P17.3's verdict STANDS, but it was
                                                right by luck, not by construction.
compressible_momentum/tools/run_gate.sh:29,39   THE FAILURE. Fixed here.
ideal_gas_eos/tools/run_gate.sh:88              delta REPORTING only, no criterion. The
                                                EOS files are clean vs HEAD, so the numbers
                                                printed were correct.
```

So: one false alarm, one corrupted table row, one prior phase correct by luck, and the
fingerprints untouched. Nothing already reported as PASS becomes FAIL.

## 8. What is NOT being changed

The criterion text is untouched — L5 still demands `physics::assembleDiffusionContribution`
be untouched, at full strength. No threshold moved, no test was relaxed, and the original
defective output is preserved verbatim in `PRESERVED_invalid_L5_instrument/`.
