# P13-006 — Packaging: acceptance gate

Frozen before any production change, on the tree that closed P13-005.

Stop at the first failure. No threshold weakened; amendments recorded separately. Every baseline
below is **measured** (see `audit.md`), never inferred.

---

## The contract this gate must not break

`.github/workflows/release.yml` globs `build/CFDApp-*-Windows-x64.zip` and `...-Windows-x64.exe`,
and the Windows packaging was verified on real hardware for 0.1.0 and v0.2.0. **A Windows release
build must still produce exactly `CFDApp-<version>-Windows-x64`.** That cannot be measured on this
host, so it is made testable a different way: the name and generator decision moves into a pure
CMake function that can be driven for any system with `cmake -P`, and Windows is asserted there.

## Group N — the package tells the truth about itself  (DETECTION)

| id | criterion | measured baseline |
| --- | --- | --- |
| N1 | The package file name names the platform it was actually built for. | `CFDApp-0.2.0-Windows-x64.zip` **on Linux** |
| N2 | On Windows the name is byte-identical to `CFDApp-<version>-Windows-x64`. | (unchanged; asserted via the function) |
| N3 | The package version includes `CFDAPP_VERSION_SUFFIX`, so a dev build cannot be named like a release. | package `0.2.0` vs binary `0.2.0-dev` |
| N4 | `file` on the packaged `cfdapp` agrees with the platform in the package name. | ELF/GNU-Linux inside a `Windows-x64` archive |

## Group G — generators  (DETECTION)

| id | criterion | measured baseline |
| --- | --- | --- |
| G1 | `cpack` exits **0** on this Linux host. | exit **1** (NSIS) |
| G2 | NSIS is requested only for Windows. | requested unconditionally |
| G3 | On Windows the generator list is still exactly `ZIP;NSIS`. | (unchanged; asserted via the function) |

## Group C — a CLI-only package  (DETECTION)

| id | criterion | measured baseline |
| --- | --- | --- |
| C1 | With `CFDAPP_BUILD_GUI=OFF` and packaging ON, `CPackConfig.cmake` is produced and `cpack` exits 0. | no `CPackConfig.cmake` at all |
| C2 | The CLI-only package contains `bin/cfdapp`, `examples/`, `docs/`, `licenses/` — and **no** `cfdapp_gui` and no Qt runtime. | n/a (no package existed) |
| C3 | The GUI package still contains both executables. | both present |

## Group P — preservation

| id | criterion | measured baseline |
| --- | --- | --- |
| P1 | The install tree stays clean: no `CMakeFiles`, `*_autogen`, `*.o`, `*.a`, `CMakeCache.txt` or test binaries; `bin/` holds only the expected executables. | clean; `bin/` = 2 entries |
| P2 | The **installed** `cfdapp` runs an **installed** example, from a working directory outside the tree. | exit 0 |
| P3 | The install tree still carries `examples/`, `docs/`, `licenses/`. | present |
| P4 | The full regression passes: Release+CUDA, Debug+GUI, CPU-only, clang-format-18 at CI scope. | passing at P13-005 |
| P5 | A default build (packaging OFF) configures and builds exactly as before. | passing |

## Group W — what is deliberately NOT claimed

| id | statement |
| --- | --- |
| W1 | No Windows package is built, tested or claimed by this item. The 0.1.0 / v0.2.0 Windows evidence is **cited**, never re-asserted. |
| W2 | DEB and RPM are not enabled. CPack offers them on this host; an untested `.deb` is exactly the "package/platform support that was not tested" the brief forbids claiming. |
| W3 | macOS is not claimed. The naming function handles `Darwin` so the decision is total rather than silently falling through, but nothing was built or run there. |
| W4 | No library/header install and no CMake package config (`audit.md` F6). Recorded as debt, not implemented. |

## Group NV — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| NV1 | Restore the hardcoded `-Windows-x64` name | N1, N4 |
| NV2 | Drop the suffix from the package version | N3 |
| NV3 | Force NSIS into the generator list on Linux | G1 |
| NV4 | Restore the `if(NOT CFDAPP_BUILD_GUI) return()` guard | C1 |
| NV5 | Make the naming function answer for an unknown system | it must refuse, not invent a plausible name |
| NV6 | Point the install-cleanliness check at a build tree | P1 — the check must be shown able to fail |

---

## Dry-run record, measured on the unchanged tree

```text
N1  package name on Linux              CFDApp-0.2.0-Windows-x64.zip   -> names the real platform
N3  package version vs binary          0.2.0  vs  0.2.0-dev           -> equal
N4  packaged binary type vs name       ELF GNU/Linux in Windows-x64   -> agree
G1  cpack exit status here             1                              -> 0
C1  CPackConfig.cmake with GUI=OFF     not produced                   -> produced
P1  install tree                       clean, 125 files, bin/ = 2     PRESERVATION
P2  installed cfdapp runs an example   exit 0                         PRESERVATION
```

N1/N3/N4/G1/C1 each have a measured baseline differing from its target. P1–P5 are PRESERVATION and
already pass, which is correct for that kind and is **not** counted as an achievement.

## Gate verdict

P13-006 closes only when N, G, C and P pass, every NV control fails, and the W statements are true
of what was actually done.
