# P12-DIFF-002-FORMAT-001 — clang-format-18 clean tree (layout only, proven)

DIFF-002's W10 requires a clang-format-clean tree, and so does CI's `format` job. Before this
step, 18 of 567 files had formatting violations:

| origin | files |
|---|---|
| DIFF-002 | `NonOrthogonalDiffusion.cpp`, `MeshGeometry.cpp`, `BoundaryFluxProbe.hpp`, `test_thermal_boundary_consistency.cpp` |
| MESH-007 | `Mesh.cpp`, `MeshMotion.cpp`, `AlePISO.cpp`, `PISO.cpp`, the three ALE test files, `MeshMotionCases.hpp` |
| UC-001 / UF-001 | the Poiseuille and natural-convection tests |
| this session | the three edited tests |

## Proof that only layout changed

1. **Production object code: bit-identical.** After `clang-format-18 -i`, the Release
   `libcfdcore.a` rebuilds to `143a1dda0680bc1d…`, the frozen library
   ([logs/01_format.log](logs/01_format.log)). Archives are deterministic.
2. **Every file, token level** ([tools/token_proof.py](tools/token_proof.py),
   [logs/02_token_proof.log](logs/02_token_proof.log)). For all 18 files:
   - the code with comments stripped and all whitespace removed is **identical**;
   - `#include` sets are **identical**;
   - the comment text is **identical word for word**.

   One file, `test_thermal_boundary_consistency.cpp`, had an `#include` moved by `SortIncludes`.
   Its behaviour is covered by the W10 regression.
3. **Remaining violations:** 0 of 567 files.

`data/before/` keeps every pre-format file.

## Process error, recorded

`format.sh`'s one-line summary said "ALL IDENTICAL" while three per-file lines said DIFFERENT. The
flag was set inside a `$( … )` subshell and never propagated.

The per-file lines were correct. The first check, a raw non-whitespace stream, is too strict: it
flags comment reflow (the `//` markers move between words) and include sorting. The comment-aware
proof above supersedes it; `01_format.log` is kept unchanged.

## Consequences for earlier freezes

The frozen W8B and LOWMACH-001 candidate test files equal these files **before** formatting
(`data/before`):

| file | candidate / before | after formatting |
|---|---|---|
| `test_structured_quad_production_case.cpp` | `7acf926d…` | `41a4d506…` |
| `test_multiblock_production_case.cpp` | `b2b87bb7…` | `7176b3de…` |
| `test_low_mach_regression.cpp` | `e69d3a62…` | `593165a1…` |

The repository files now differ from those candidates by layout only, as proven above. From here
on, integrity checks use the post-format hashes for these files, and for the two production files
whose layout changed:

| production file | frozen | post-format |
|---|---|---|
| `NonOrthogonalDiffusion.cpp` | `20a02b16…` | `48588d29…` |
| `MeshGeometry.cpp` | `04c964c2…` | `a2bfb9f5…` |

The library hash `143a1dda…` is the invariant that covers them.
