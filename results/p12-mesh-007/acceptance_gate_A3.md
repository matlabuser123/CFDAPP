# P12-MESH-007 — Amendment A3: the G9 baseline (pre-registered)

Authorized on 2026-09-17 by the user's instruction to close the P12-DIFF-002 → P12-GRAD-002 →
P12-MESH-007 chain, which lists G9 among the MESH-007 items to complete.

**This document is separate** from `acceptance_gate.md`. The gate, its amendments A1 and A2
(`5b45fed9…`), and the original G6.3 failure ([logs/12](logs/12_gate_stage3_G1.2_G5_G6_G7_G8.log))
stay unchanged and on record. The unchanged G6.3/G7.3 rerun (logs 20–22) and G2.3 (log 26) passed
before this amendment was frozen.

**A3 amends G9's reference only.** No criterion, threshold, run, mesh or probe list changes. G10.3's
tolerance for the known `MeshQualityReport.DisconnectedMeshIsFatal` defect is not needed:
P12-ASAN-001 fixed that test, so G10.3 is evaluated with **zero** permitted failures.

## 1. Why G9's frozen reference no longer isolates MESH-007

G9 defines BASE as "the pre-MESH-007 working tree" (`$HOME/m7ref/base`) and requires BASE and NEW
to agree bitwise (G9.1), and byte for byte at the CLI (G9.2). The purpose of G9 is **backward
compatibility of MESH-007's own changes**.

Since MESH-007 stopped, separately authorized and separately gated phases have changed the static
numerics on purpose:

| phase | change |
| --- | --- |
| **P12-GRAD-002** (with DRIFT-001, VAL-001, A2 and A3) | the Green–Gauss boundary gradient; two committed cases select Rhie–Chow |
| **P12-DIFF-002** (with its sub-phases) | the Dirichlet wall diffusion flux, and the thermal-interface and solver-robustness fixes |
| **P12-ASAN-001** | a test-only lifetime fix |
| **FORMAT-001** | layout only |

Each of these carries its own backward-compatibility evidence (GRAD-002 C8/C11, DIFF-002 W9A).
Against the frozen BASE, G9.1 and G9.2 would therefore fail because of those phases, not because
of MESH-007, and G9 would not measure what it was written to measure.

## 2. The amended reference

**BASE-A3 = nom7:** the current working tree with **every MESH-007 change removed** and every later
phase kept. It is built by [tools/build_nom7.sh](tools/build_nom7.sh) (Release, GUI off, the same
dependency sources as `build/release`). The removal rules follow from a full diff of the
pre-MESH-007 tree against the current one.

**Removed or restored:**

- **MESH-007's new files are removed:** `MeshMotion`, `AlePISO`, `PisoStep.hpp`, and its four test
  files.
- **The files only MESH-007 changed return to their BASE version:** `Cell.hpp`, `Face.hpp`,
  `Mesh.{hpp,cpp}`, `TimeDerivative.{hpp,cpp}`, `MassFlux.{hpp,cpp}`,
  `TransientMomentum.{hpp,cpp}`, `PISO.cpp`, `TransientSolver.{hpp,cpp}`, `src/CMakeLists.txt`,
  and two test `CMakeLists.txt`.
  - Of these, **`PISO.cpp` is the only one where MESH-007 changed existing lines** (9 lines, the
    momentum-assembly and CFL call sites, which moved into `detail::solvePisoStep`).
  - Every other MESH-007 change is an insertion.

**Shared files:**

- **`MeshGeometry.{hpp,cpp}`:** only MESH-007's inserted blocks and the includes only they use are
  removed. The GRAD-002 and DIFF-002 insertions (`boundaryLineIntersection`,
  `boundaryInwardStencil`) stay.
- **`tests/unit/discretization/CMakeLists.txt`:** MESH-007's lines are removed.
- **The one later test that needs the motion API** is removed from nom7:
  `test_gradient_boundary_consistency.cpp` (GRAD-002 A2). It writes no output file.

**Fidelity of the reference.** This is a precondition, not a result. It is checked automatically by
[tools/nom7_fidelity.py](tools/nom7_fidelity.py) on every nom7 build over `src/`, `include/`,
`tests/` and `apps/`, with generated `results/` directories excluded. A failure stops the build
script.

- **(F1)** The set of files in which nom7 differs from the current tree **equals** MESH-007's file
  list above, plus the one removed later test.
- **(F2)** Every file in which nom7 differs from BASE is attributed to a later phase: its path is
  named in the evidence of DIFF-002, GRAD-002 or ASAN-001. No file that only MESH-007 changed
  differs from BASE.
- **(F3)** In `MeshGeometry`, nom7 differs from BASE by insertions only, and none of them is marked
  MESH-007.
- **(F4)** nom7 builds in Release with 0 warnings.
- **(F5)** The nom7 build is reproducible. The fresh execution rebuilds nom7 from an empty directory
  and requires the library hash recorded at the freeze.

## 3. G9 as evaluated under A3

The text of G9 is unchanged. BASE reads nom7, and NEW reads the current tree (`build/release`,
library hash recorded at the freeze and re-verified at execution). The driver is
[tools/g9_a3.sh](tools/g9_a3.sh) (`dry` → logs 30–35, `fresh` → logs 40–45).

| id | as frozen | executed by |
| --- | --- | --- |
| G9.1 | probe programs compiled against nom7 and NEW print **bitwise identical** output: static PISO for 10 steps on the C16 cavity and the G16, Q16 and MB2 channels; a TransientSolver run of the C16 cavity; the MESH-005 and MESH-006 probes (2D SIMPLE); 3D SIMPLE for 20 iterations on the 8³ lid cube; the ThermalSolver in 2D and 3D; every builder's geometry for C16, G16, Q16, MB2 and H8 | [tools/bitprobe7.cpp](tools/bitprobe7.cpp) covers the list. `results/p12-mesh-005/tools/bitprobe.cpp` and `results/p12-mesh-006/tools/bitprobe6.cpp` run unchanged on their two programmatic meshes and **every 2D committed case**, as MESH-006's G10.1 ran them. A case is 2D when its mesh file has no `nz`; the included cases are listed in the log. 3D coverage is bitprobe7's lid cube and G9.2. |
| G9.2 | CLI, every committed case and CLI fixture (2D and 3D): `fields.csv`, `solution.vtk` and `residuals.csv` byte-identical; `metadata.json` equal as parsed JSON; stdout identical; exit codes equal | [tools/g9_compat.sh](tools/g9_compat.sh), MESH-005's `compat.sh` procedure with the two binaries replaced |
| G9.3 | no existing case, fixture or test input is modified **by MESH-007** | [tools/g93_inputs.py](tools/g93_inputs.py) checks three things: MESH-007's file list contains no case or test input; nom7's and the current tree's `cases/` and `tests/data/` are identical; every BASE-vs-current change there is attributed to a later phase |
| G9.4 | regenerated tracked outputs differ from the reference only in timing fields | nom7's full suite and the full suite of an isolated copy of the current tree (built the same way) run from the same snapshot of the generated outputs (ctest `-j16`). The outputs are classified with MESH-006's classifier (via `results/p12-grad-002/a2/tools/classify_scope.py`); 0 VALUES, 0 NEW and 0 GONE are required |
| G9.5 | the existing PISO, transient and restart suites and the MESH-001–006 suites pass by name | G10's full regression |

**Fail-closed rules.** An agreement that could be vacuous never counts as a pass.

- **G9.1:** every probe run of every variant (nom7, NEW, NEW again, cur, nograd) must exit 0 and
  print every expected item. bitprobe7 has 13 items; probes 5 and 6 print both programmatic meshes
  and every included case. Otherwise the comparison is **INVALID**. The G9.1 verdict line requires
  all of this plus nom7 == NEW and NEW == NEW for all three probes.
- **G9.2:** each side must show the expected outcome, otherwise the case is **INVALID**, never
  IDENTICAL.
  - A committed case must exit 0 and write all four files: `metadata.json`, `residuals.csv`,
    `fields.csv` and `solution.vtk`.
  - A CLI fixture must exit with the code the test suite asserts: `tests/CMakeLists.txt`, and
    `runCaseAndAssertConverged` for `valid_cavity`. The table is in `g9_compat.sh`.
  - A fixture that exits 0 must write the four files; one that exits 3 must write `metadata.json`
    and `residuals.csv`.
  - A fixture missing from the table is INVALID.
  - Differences are collected before the verdict, so a metadata-only or stdout-only difference
    cannot be reported as IDENTICAL. The inherited script appended them to "IDENTICAL".

**G9.2's `metadata.json` rule, kept as frozen.** "Equal as parsed JSON" includes every key.
MESH-005's procedure compares all keys too. Control **C1** runs the same binary twice before G9.2
is evaluated. It shows whether a wall-clock key makes identical runs differ; such a key would be
reported, never waived silently.

## 4. Non-vacuity (dry-run, recorded before this amendment is frozen)

- **(N1)** Every G9.1 probe must resolve a static-numerics change. It is compiled against GRAD-002's
  attribution pair (cur and nograd), which differ only in `Gradient.cpp`, and must print different
  output for each of bitprobe7, probe 5 and probe 6.
  - For bitprobe7, derived from that one-file difference, the geometry items must be identical and
    the non-orthogonal "Q16 channel" items must differ.
- **(N2)** Each probe's output on NEW, run twice, is byte-identical.
- **(N3)** The CLI comparison flags a known difference: nograd's `cfdapp` against NEW's differs
  on `poiseuille_distorted`.
- **(C1)** NEW's `cfdapp` against itself: every case and fixture IDENTICAL (and valid).
- **Instrument self-tests** (dry-run support, not gate items):
  - G9.1 (`g91selftest`): the fail-closed checks must accept the real outputs and reject four
    corruptions, including a replica of the vacuous first dry-run.
  - G9.2 (`g92selftest`): fake binaries must make a crash and a missing export INVALID, and a
    metadata-only change DIFFERENT, while real runs stay IDENTICAL.
  - G9.3 (`--self-test`): a planted input change in nom7, and an unattributed input, must each fail
    G9.3.

The dry-run record, including the attempts that exposed the instrument defects above, is in
[a3/dryrun.md](a3/dryrun.md).

## 5. Stop rule

MESH-007's stop rules apply unchanged: stop at the first failed item, change nothing after seeing a
result, and preserve failed runs.

- **G6.3 and G7.3 were rerun first, as frozen** ([tools/gate_rerun.sh](tools/gate_rerun.sh), logs
  20–22), and passed.
- **If any part of G9 fails or is INVALID** in the fresh execution, the failure is preserved and
  reported to the user with a classification before anything is changed (user instruction,
  2026-09-17).
- **The fresh execution runs only the artifacts the freeze log hashes** (`logs/39_a3_freeze.log`).
  `g9_a3.sh fresh` re-verifies every hash and the NEW library before its first stage, and the nom7
  library after rebuilding it, and fails closed on any mismatch.
