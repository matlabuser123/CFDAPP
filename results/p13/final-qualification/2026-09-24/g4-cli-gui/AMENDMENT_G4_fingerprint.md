# AMENDMENT — the qualification source fingerprint: V1 withdrawn, V2 adopted

Authorized 2026-09-25. V1 is preserved as historical evidence and is **not**
recomputed, re-derived or deleted.

---

## 1. V1 — the original definition

Implemented by `results/p13/final-qualification/2026-09-24/tools/tree_fingerprint.sh`,
unchanged and still present. Its source set, quoted from its own design note:

> The SOURCE one excludes `results/` and is the one that must NOT move. It covers
> what is actually built and tested: `src/`, `include/`, `cuda/`, `tests/`,
> `apps/`, `benchmarks/`, `scripts/`, CMake files, `CLAUDE.md`, **`TODO.md`**.

Algorithm:

```text
V1_tracked    = sha256( git diff --binary HEAD -- . :(exclude)results/ )
V1_untracked  = sha256( sha256sum of each untracked non-results file, sorted )
V1            = sha256( "<V1_tracked> <V1_untracked>" )
```

**Original V1 value, recorded at G1 and unchanged through G3:**

```text
86346f151e5359e37d23b13fca2cdc7f5549c88900a9f9dc7e77e58973fc8d5f
```

## 2. Why V1 became unsuitable

`TODO.md` is inside V1's source set, and `TODO.md` is the qualification's own
execution control file: closing a gate means ticking its boxes there. So V1
cannot be held constant across any gate closure — **closing a gate is what
breaks it.**

This is measured, not argued:

```text
label                       date                TODO.md changed lines   V1
fingerprint_G1_baseline     2026-09-24T01:56Z   667                     86346f15...
fingerprint_G3_case_val...  2026-09-24T18:59Z   667                     86346f15...
fingerprint_G4_before       2026-09-24T19:34Z   496                     differs
```

V1 held for exactly as long as `TODO.md` held, and moved when G1–G3 were ticked.

Attribution of that move, by elimination:

```text
tracked source files differing from the 18:59Z record   TODO.md ONLY
      (the other 162 files: identical changed-line counts)
untracked source file LIST                              identical
untracked solver-output contents                        unchanged
      (mtimes 2026-09-24T18:31Z, predating the 18:59Z record)
```

Residual uncertainty, stated rather than hidden: that 162-file comparison is on
changed-line counts, not per-file content hashes, because no copy of the 18:59Z
`TODO.md` survives — `results/todo-archive/` holds only a 2026-09-17 version — so
the historical tracked-diff hash cannot be recomputed with `TODO.md` removed.

A second, independent reason V1 was unusable on the day: it was measured while
another session's G3 controls were mid-cycle, patching and restoring
`src/io/CaseReader.cpp` and `src/io/case/JsonUtil.cpp`. Two probes three minutes
apart disagreed (`c03f7942…` vs `837fe2e5…` on the tracked component). V2 was
therefore first taken only after that session had finished and its restores were
verified.

## 3. V2 — the adopted definition

`SOURCE_FINGERPRINT_V2` = the working-tree engineering source, excluding
**exactly two** things: `results/**` and `TODO.md`. Nothing else is excluded.
Implemented by `tools/fingerprint_v2.sh`.

Exact file-selection algorithm:

```text
repo root = /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EXCLUDE   = results/**   and   TODO.md

1. tracked component
     git diff --binary HEAD -- . :(exclude)results/ :(exclude)TODO.md
     V2_tracked = sha256(that byte stream)

2. untracked component
     git ls-files --others --exclude-standard -- . :(exclude)results/ :(exclude)TODO.md
     sorted; sha256sum each file; V2_untracked = sha256(that digest list)

3. SOURCE_FINGERPRINT_V2 = sha256("<V2_tracked> <V2_untracked>")
```

### Two interpretation decisions, flagged rather than taken silently

**(a) The untracked component is INCLUDED.** The authorization says "the tracked
engineering tree". Read literally that would drop ~120 untracked P14/P15/P16
source files out of the hash — `src/turbulence/WallFunctions.cpp`,
`src/thermal/Enthalpy.cpp`, `src/io/CheckpointWriter.cpp`, the whole `cuda/`
turbulence set, and the CLI/GUI equivalence harness itself — leaving the
instrument blind to most of the new code. Including them is strictly stronger and
matches V1's own composition. **If strictly-tracked was meant, say so and V2 is
recomputed.**

**(b) Solver-output files are NOT excluded.** Two untracked directories are
solver output rather than engineering source and are rewritten whenever a suite
runs:

```text
cli_log_level_valid_cavity_cli_smoke/results/
cases/turbulent_heated_channel/results/
```

They stay inside V2 — "do not exclude arbitrary additional files merely to obtain
a match". They are additionally hashed on their own as a diagnostic so that a V2
move is attributable in one step. Across all of G4 that sub-hash never moved
(`3bf622aa…` at both ends).

### V1 and V2 are never compared numerically

They hash different file sets. No statement of the form "V2 equals/differs from
V1" appears anywhere in this gate's evidence.

## 4. The G4 invariant, and its one authorized exception

Required: `SOURCE_FINGERPRINT_V2(before) == SOURCE_FINGERPRINT_V2(after)` unless a
named, explicitly authorized engineering-source fix occurs.

```text
V2 initial            2026-09-24T19:45:24Z  6639759722a622db99c03f27cfb5855a66a2b21a9f05ff4f8714924d7ae2024b
V2 after F1+F2        2026-09-24T20:01Z     c6f01eeac4daed3cae0d34a13a7d9ab13d0362439d043150fce9dbae6f2d2156
V2 final, after F4    2026-09-24T20:2xZ     8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

**They differ, and the exception applies.** The named, explicitly authorized
changes are the three harness edits this gate was authorized to make:

```text
F1  CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli   authorized §3
F2  CliGuiEquivalenceTest.AThermalCaseAgreesBitwise                  authorized §4
F4  ClientArm / ArmResult client provenance in the equivalence harness  authorized 2026-09-25
```

Both live in one file, `apps/gui/tests/test_cli_gui_equivalence.cpp`, which is
untracked — hence the change lands in V2's untracked component.

Attribution, proven rather than asserted:

```text
                       V2 initial       V2 final (after F4)
V2_tracked             2f71bb14...  ->  2f71bb14...   IDENTICAL, no tracked file changed
solver-output sub-hash 3bf622aa...  ->  3bf622aa...   IDENTICAL
untracked ENGINEERING  a59e51fe...  ->  750012b4...   changed
untracked file count   122          ->  122           unchanged (after §5)
tracked files changed  112          ->  112           unchanged

untracked engineering files with mtime after 19:45:24Z:
  apps/gui/tests/test_cli_gui_equivalence.cpp      <- exactly one
```

All three authorized edits — F1, F2 and F4 — live in that one file. Nothing else
in the engineering tree moved across the whole of G4.

No production source, no numerics, no solver algorithm, no tolerance and no
checkpoint format was touched. `apps/gui/SimulationController.cpp` and
`apps/cli/main.cpp` were patched only *inside* controls and restored to their
exact baseline SHA-256s.

## 5. A control side effect found and cleaned, recorded not hidden

The first `V2 final` reading was `4682c8b6…` with an untracked file count of
**123**, one higher than the baseline. The extra file was `x.ckpt` (54,959 bytes,
JSON, mtime 2026-09-24T19:54:21Z) in the repository root.

Cause: control **N4** plants a defect that removes the `--checkpoint-every` value
validation, so `cfdapp --case ... --checkpoint x.ckpt --checkpoint-every ten`
stops being rejected and actually runs. `tests/CMakeLists.txt:103` and `:106` give
that checkpoint the repo-root-relative path `x.ckpt`, and the ctest working
directory is the source root — so the run wrote it there. The control restored
every source file by SHA-256 but had no notion of filesystem side effects.

Handled: the file is preserved as evidence at
`logs/N4_SIDE_EFFECT_x.ckpt` and removed from the repository root, after which
the untracked count returned to 122 and `V2 final` settled at `c6f01eea…`.

It recurred, exactly as predicted, when N4 was re-run after the F4 repair — same
54,959 bytes — and was cleaned the same way before the final V2 reading. The
closeout now checks for it explicitly rather than relying on having remembered.

Two things this exposes, neither fixed here:

* **A gap in my own control.** Restoration verified source hashes only. A control
  that can cause a write should assert the filesystem is also back.
* **Pre-existing test hygiene.** Those two rejection ctests name a repo-root
  relative `x.ckpt`. They are safe only because the invocation is rejected before
  any solve; the moment it is not, they pollute the source tree. Not G4's to fix
  (Rule 8), recorded as debt.
