# P13-002 — Robust case validation: summary

**The validation was already sound. The diagnostic was not.** This item carries the rejection as
data instead of flattening it into prose and scraping it back out downstream.

```text
gate        results/p13/case-validation/acceptance_gate.md   (frozen before any change)
amendments  acceptance_gate_A1.md   a mis-designed control, and its corrected replacement
            acceptance_gate_A2.md   the gate contradicted itself about criterion M2
            acceptance_gate_A3.md   A2 was wrong; M2 was vacuous and is withdrawn
baseline    the tree that closed P13-001
environment WSL2 Ubuntu 22.04, gcc 11.4, CUDA 12.9, RTX 5000 Ada (sm_89)
```

---

## 1. The audit overturned the premise I started with

`TODO.md` lists this item as unstarted, which invited the assumption that invalid cases get
through. A mutation matrix generated from each case's own JSON says otherwise:

```text
2537 mutations   12 case families   0 accepted
```

Every automatically-generable invalid case — wrong type, unknown enum value, missing required key,
unrecognised key, raw `NaN`, raw `Infinity`, zero and negative where positivity is required — is
already rejected **before the solver runs**, by ~151 existing rejection sites. Families covered:
2D cavity, 3D duct, thermal, species, thermal+species, multiphase, compressible, coupled
compressible, graded, multiblock, non-orthogonal quad, and a derived k-epsilon case.

So the work was not "make invalid cases fail". It was the two things the brief names that were
genuinely missing.

## 2. What was actually broken

### 2.1 The structure existed and was thrown away

`throwConfigError` computes four things to build its message:

```text
<file>: field "<field>" must satisfy <constraint>; received <received>
```

and then flattens them. Downstream, `apps/gui/SimulationControllerEditing.cpp` recovered them by
**searching the text** — in three separate places:

```cpp
sectionForMessage(message)                   // scans for "mesh.json", "physics.json", ...
fieldForMessage(message)                     // scans for the token: field "
message.contains("mesh is invalid")          // decides the mesh-quality panel's STATE
```

The CLI discarded the structure entirely. `ValidationPanel.qml` renders a per-field slot that only
that scraping ever filled.

### 2.2 It was fragile, absent, and coupled — all three measured

| | evidence |
| --- | --- |
| **fragile** | control **N1b**: renaming one token (`field "` → `key "`) broke the GUI's field extraction, and no test noticed |
| **absent** | 60 of 63 flagged rows — every manifest file reference, all 12 families — plus 16 `CaseBuilder` and 2 `CaseReader` sites that never carried a field at all |
| **coupled** | the mesh-quality panel's status decided by a prose substring, with no test of the coupling |

## 3. The change

```text
Exception.hpp     + CaseDiagnostic{file, field, constraint, received}
                  + payload on CaseConfigurationError and IOError
                  + caseDiagnosticOf(const Error&) -- one place to ask
JsonUtil.cpp      throwConfigError populates it -> ~140 sites instrumented,
                  none of them touched, message text byte-unchanged
CaseReader.cpp    the manifest reference now names case.json and the key
                  (type stays IOError -- criterion S6)
CaseBuilder.cpp   the mesh-quality refusal labelled field "quality"
ProjectRunner     + caseDiagnostic on ProjectRunResult
SimulationController  all three scrapes replaced; the scrapers KEPT as the
                  fallback for rejections that carry no payload
```

The scrapers are kept deliberately. Deleting them would turn "no payload" into "no field at all"
for the 18 sites that raise directly — exactly the errors that are hardest to diagnose.

**The message text does not change.** That is criterion V3, and it is measured, not asserted:

```text
2532 of 2537 messages byte-identical
   5 changed per family = exactly the manifest references criterion S3 authorises
```

## 4. Results

```text
M1   2537 mutations, 0 accepted                          preserved
M2b  weak  63 -> 3   (the criterion)                     PASS   non-vacuity: N3
M2   vague  0 -> 0                                       WITHDRAWN -- vacuous, see A3
V3   2532/2537 messages byte-identical                   PASS
V1   the 17 CLI case fixtures                           17 / 17
V2   full Release + CUDA CTest                           2030 / 2030, 0 failed, 197.8 s
V4   CPU-only build (CFDAPP_ENABLE_CUDA=OFF)             1964 / 1964, 0 failed
     nvcc invocations 0, libcudart 0, libcfdcuda.a not built
V5   clang-format, 603 files                             0 violations
V6   P13-001 checkpointing must not regress              62 / 62
S1   the payload on ~140 sites                           PASS   7 unit tests
S3   manifest names case.json and the key                PASS
S4   GUI reads the diagnostic, not the message           PASS   39/39 GUI tests
S6   exception types unchanged                           PASS   asserted by type in the test

N1   (mis-designed -- see A1)                            preserved, proves nothing
N1b  rename the marker, BEFORE the change                DETECTED -- the GUI broke
N2   remove the payload                                  DETECTED -- 2 tests fail
N3   revert the manifest fix                             DETECTED -- weak 3 -> 5
N4   point the matrix at a dead baseline                 DETECTED -- harness refuses

regression: rename the marker, AFTER the change          6/6 validation tests still pass
```

The last line is the item's point. The identical experiment that broke the GUI before is now inert.

The 3 residual `weak` rows are cascade messages — removing `interfaces` correctly reports the
`patches` coverage failure; removing the species block correctly reports the orphaned
`boundaries.json` entry. They name a file and a field, just not the one mutated, and demanding
otherwise would demand worse messages.

## 5. Three amendments, and what they cost

This item's gate was amended three times. All three were my errors, none was a weakened threshold,
and each is preserved rather than edited away.

**A1 — a control that could not have worked.** N1 reworded `must satisfy` → `must be` and the GUI
test still passed, which I first read as "premise refuted". It was not: `fieldForMessage` reads the
`field "` marker and N1 changed a different clause. A control must perturb the input the mechanism
actually consumes; a plausible-looking perturbation is not a load-bearing one. N1b renamed the
marker itself and the GUI broke.

**A2 — the gate contradicted itself.** Criterion M2's prose said "names neither file nor field"
(AND); the instrument implemented "names not both" (OR); and the baseline figure quoted in the
frozen text came from the instrument. Two rules, one document.

**A3 — A2's own numbers were wrong, and a control found it.** A2 asserted the AND-rule baseline was
3. Re-derived from the frozen corpus it is **0** — so criterion M2 was already satisfied before any
code changed and could never fail. Control N3 surfaced it by doing the one thing that separates a
criterion from an observation: revert the fix and check the number moves. It didn't. M2 is
withdrawn; **M2b (63 → 3) is the criterion**, and N3 proves it non-vacuous.

The root error in A2/A3 was **inferring a baseline instead of measuring one**, when the numbers
were in the frozen CSVs the whole time. This project already has a name for that failure — a
criterion the baseline passes vacuously is un-dry-run — and writing it into an amendment did not
make it less so.

## 6. Instrument defects, preserved

* The matrix reported **ten findings across two runs that were all its own fault**: it treated
  documented optional defaults (`format_version`, `reynolds_number`, `coupled`,
  `non_orthogonal_corrections`) and free-text labels (case name, species/phase name) as required,
  enumerated fields. Corrected by naming each explicitly, citing the parser line that makes it so —
  never by loosening the pass criterion.
* The `unknown` mutation was mis-attributed to whichever leaf the sweep was visiting rather than to
  the object it added a key to. Applying it once per object removed ~40 duplicate rows.
* A control script's embedded heredoc broke, its trap did not fire, and it **left production source
  modified**. Caught by `git status`, not by the script. Restored byte-exactly from HEAD; every
  control since puts its patch in a separate file and **verifies its own restore by hash**.

## 7. What this item deliberately did not do

* **No parser rewrite.** 151 rejection sites already carry (file, field, constraint, received).
* **No change to the GPU→CPU linear-solver fallback.** Documented, deliberate behaviour
  (P6-GPU-002), reported through `SolverResult::backendUsed`. A fallback is not an unsupported
  configuration.
* **No cross-patch well-posedness check** (an inlet with no outlet, an open boundary with no
  pressure reference). The matrix cannot reach it and the correct criterion is not obvious; recorded
  as technical debt rather than guessed at.
* **No mutation inside lists of objects** (species entries, multiblock blocks, boundary patches) —
  that needs per-schema knowledge, which is the assumption this sweep exists to avoid. Covered
  instead by the existing `bad_*` fixtures.
