# P13 Final Qualification — G7, Packaging: final report

**Result: all criteria PASS. G7 → PASS.**

Date 2026-09-24 (UTC) / 2026-09-25 local. Tree `3bd32f918656-modified`.

**No production, test or harness source was changed.** `SOURCE_FINGERPRINT_V2` is
byte-identical before and after — no delta to account for.

---

## 1. Scorecard

```text
Correct platform naming          PASS       9 derivations + 2 registered ctests
Correct version identity         PASS       one source drives filename and binary
CLI-only packaging               PASS       GUI=OFF configures, builds, packages
Extracted package smoke test     PASS       packaged cfdapp ran a packaged example
Binary type verified             PASS       ELF x86-64 GNU/Linux, by `file`, not by name
Packaging controls               DETECTED   6 of 6
Per-control restoration          VERIFIED   4 of 4 files, checked twice independently
Filesystem hygiene               PASS       artifacts identified; no strays
SOURCE_FINGERPRINT_V2            MATCH      before == after, no delta

Native Windows package           NOT CLAIMED
DEB / RPM                        NOT CLAIMED
macOS .dmg                       NOT CLAIMED
library / header install         NOT CLAIMED

G7 -> PASS
```

## 2. Step 2 — platform naming

Two separate things, reported separately so neither borrows credibility from the
other.

**Linux, actually built and packaged:**

```text
CFDApp-0.2.0-dev-Linux-x64.tar.gz      generator TGZ;STGZ      CPACK_SYSTEM_NAME Linux
```

**The Windows contract, derivation only.** The name and generator decision lives
in a pure CMake function that `cmake -P` drives for any target system, so the
contract `release.yml` depends on is asserted without pretending a Windows
package was produced. All nine derivations hold:

```text
windows release          CFDApp-0.2.0-Windows-x64        [ZIP;NSIS]  <- release.yml globs this
windows x86_64 spelling  CFDApp-0.2.0-Windows-x64        [ZIP;NSIS]
windows dev              CFDApp-0.2.0-dev-Windows-x64    [ZIP;NSIS]
linux release            CFDApp-0.2.0-Linux-x64          [TGZ;STGZ]
linux dev                CFDApp-0.2.0-dev-Linux-x64      [TGZ;STGZ]
linux arm64              CFDApp-0.2.0-Linux-arm64        [TGZ;STGZ]
macos arm64              CFDApp-0.2.0-macOS-arm64        [TGZ]
NSIS on Windows only     ok
false-constant output variable names (n/no/off/ignore)   ok
```

And it **refuses** rather than inventing a plausible name:

```text
cfdapp_package_naming: unsupported system "Haiku". Packaging is defined for
Windows, Linux and Darwin. Add the platform deliberately -- with something
actually built and run on it -- rather than letting the package take a name
nobody has verified.                                          exit 1
```

Both are permanent ctests, not one-off scripts: `CFDAppPackageNaming` and
`CFDAppPackageNamingRefusesUnknownSystem`, 2/2 passing.

```text
Linux package naming          PASS
Windows naming contract       PASS (derivation only)
Native Windows package        NOT RUN / NOT CLAIMED
```

## 3. Step 3 — version identity, one source

```text
CMakeLists.txt   project(CFDApp VERSION 0.2.0)
                 CFDAPP_VERSION_SUFFIX "dev"
      ->  CPACK_PACKAGE_VERSION    "0.2.0-dev"
      ->  package filename          CFDApp-0.2.0-dev-Linux-x64
      ->  cfdapp --version          CFDApp 0.2.0-dev
```

Nothing is hard-coded in the harness: criterion N3 reads the version out of the
freshly built binary and requires the package name to contain it. A release
package cannot be named `0.2.0` while the executable says `0.2.0-dev`, and
because the suffix is *in the name*, `release.yml`'s `CFDApp-*-Windows-x64.zip`
glob cannot pick up a dev build by accident.

## 4. Steps 4–6 — CLI-only packaging, and the extracted smoke test

The GUI was **not** enabled to make packaging pass. Both trees are configured
from scratch (`rm -rf` first) with `CFDAPP_ENABLE_CUDA=OFF`, Release, Ninja:

```text
build/pkg     CFDAPP_BUILD_GUI=ON  + packaging ON     configure, build, cpack exit 0
build/pkgcli  CFDAPP_BUILD_GUI=OFF + packaging ON     CPackConfig.cmake produced,
                                                      cpack exit 0
```

Contents, which is where C2/C3 bite:

```text
GUI package bin/    cfdapp  cfdapp_gui
CLI package bin/    cfdapp                 no cfdapp_gui, no Qt runtime
CLI package also    examples/  docs/  licenses/
```

The size difference is independent corroboration rather than another assertion:
the CLI archive is 933,895 B against the GUI's 1,994,954 B — 47%. Nothing Qt-shaped
rode along.

**The extracted smoke test ran outside the build tree entirely.** The package was
extracted to `/tmp`, the extracted binary was invoked from `/tmp` against an
example taken from inside the package, and the original build-tree executable was
never used:

```text
packaged cfdapp  ->  packaged example annular_sector_conduction_multiblock
                     Case: Annular Sector Conduction (3-block multiblock)
                     Converged: yes            exit 0
```

A genuine multiblock conduction case, not a placeholder.

Install trees are clean (criterion P1): 131 files for the GUI configuration, 130
for CLI-only, `bin/` holding only the expected executables, and zero
`CMakeFiles`, `*.o`, `*.a`, `*autogen*`, `CMakeCache.txt` or test binaries.

## 5. Step 5 — exactly which artifacts G7 qualifies

Every pre-existing `CFDApp-*` archive under `build/` was deleted before this run,
so each of these is provably produced by it. All four carry mtime 2026-09-25 07:30:

```text
build/pkg/CFDApp-0.2.0-dev-Linux-x64.tar.gz     1,994,954 B
    sha256 c3b21fedb302d6001ff01e9b8fc95550026e326475ec7653d32686a71848103f
build/pkg/CFDApp-0.2.0-dev-Linux-x64.sh         1,998,898 B
    sha256 bd5f22537a07906ace037ad975ced943b39811dc8167075e418f9a0def34b3d5
build/pkgcli/CFDApp-0.2.0-dev-Linux-x64.tar.gz    933,895 B
    sha256 ca0d56dd2de412e68b183643ce477eafcb387de35b15e51d5374fd6b9bf0d2f5
build/pkgcli/CFDApp-0.2.0-dev-Linux-x64.sh        937,829 B
    sha256 c2410fe4171e0f0c4bc49c62f6b7bd6570dd68f91ff20b2eb78b90c468115077

generator      TGZ;STGZ
version        0.2.0-dev
platform       Linux            CPACK_SYSTEM_NAME
architecture   x86_64           uname -m of the producing host
```

No P13 or P16 artifact was reused. All four were re-hashed after the controls ran
and are unchanged, so no control replaced or disturbed them.

## 6. Step 7 — binary type, by inspection

```text
cfdapp      ELF 64-bit LSB pie executable, x86-64, version 1 (GNU/Linux), dynamically linked
cfdapp_gui  ELF 64-bit LSB pie executable, x86-64, version 1 (GNU/Linux), dynamically linked
package name says: CFDApp-0.2.0-dev-Linux-x64.tar.gz
```

Determined with `file`, on the binary extracted from the archive — not from the
filename. Linux package + ELF binary: **VALID**. The historical defect this
guards, an ELF binary inside a `Windows-x64` archive, is what control NV1
reproduces on demand.

## 7. Step 8 — generator failure behaviour

```text
G1  cpack exit status on this Linux host          0   (baseline was 1)
G2  NSIS mentioned anywhere in the cpack log      never
G3  on Windows the generator list is ZIP;NSIS     asserted via the naming function
```

NSIS is requested only where it can exist, so the historical failure — `cpack`
dying on a missing `makensis` *after* another artifact had already been emitted —
cannot recur. No extra tooling was installed to avoid the failure path; control
NV3 exercises it deliberately instead, forcing NSIS onto Linux and confirming
`cpack` then exits 1.

## 8. Step 9 — non-vacuity controls

All six frozen controls, run unmodified and serially.

```text
NV1  hardcode the Windows name again
     planted defect YES   criterion fails YES   DETECTED   RESTORE VERIFIED
       "a Linux build is named Windows again", and the naming ctest also fails
NV2  drop the suffix from the package version
     planted defect YES   criterion fails YES   DETECTED   RESTORE VERIFIED
       the package no longer carries the binary's version
NV3  force NSIS into the generator list on Linux
     planted defect YES   criterion fails YES   DETECTED   RESTORE VERIFIED
       cpack exits 1
NV4  restore the GUI-only guard
     planted defect YES   criterion fails YES   DETECTED   RESTORE VERIFIED
       no CPackConfig.cmake for a CLI-only build
NV5  ask the naming function about an unknown system
     planted defect n/a   criterion fails YES   DETECTED
       refused with exit 1 rather than inventing a name
NV6  point the install-cleanliness check at a build tree
     planted defect n/a   criterion fails YES   DETECTED
       498 build-only artifacts found -- the check can fail
```

Each control breaks the criterion it names, not something adjacent: NV1 is checked
by the package name *and* by the ctest, NV2 against the binary's own reported
version, NV3 against `cpack`'s exit status, NV4 against the presence of
`CPackConfig.cmake`.

## 9. Steps 10–11 — restoration and hygiene

Restoration verified **twice, independently**: the frozen script checks its own
restore by sha256 after each control, and this gate's wrapper kept its own
pre-control hashes and re-checked afterwards.

```text
cmake/PackageNaming.cmake                    OK
cmake/Packaging.cmake                        OK
tests/cmake/test_package_naming.cmake        OK
tests/cmake/test_package_naming_refuses.cmake OK
control markers in cmake/ or tests/          none
repo-root CFDApp-* strays                    none
control scratch build trees left behind      none
CFDApp-* archives under build/               exactly the 4 above, all 07:30
the 4 G7 artifacts re-hashed after controls  all OK, unchanged
```

After restore the naming contract re-measures clean: all expectations met, the
refusal still exits 1, and both ctests pass.

## 10. An evidence-integrity event, recorded

`verify_packages.sh` writes into `results/p13/packaging/logs/10..16.log`, which is
the committed P13-006 evidence. Running it necessarily rewrites those files. They
were copied into this gate's tree first, as `logs/PRESERVED_2026-09-21_*`, and
the outcome is measured rather than assumed:

```text
10_configure_gui   rewritten   content IDENTICAL
11_build_gui       rewritten   content DIFFERS     <- original survives only in the copy
12_configure_cli   rewritten   content IDENTICAL
13_build_cli       rewritten   content DIFFERS     <- original survives only in the copy
14_cpack_gui       rewritten   content IDENTICAL
15_cpack_cli       rewritten   content IDENTICAL
16_packaged_run    rewritten   content IDENTICAL
```

Five are byte-identical because configure and cpack output is deterministic; the
two build logs differ, as build logs do. Without the copy, the 2026-09-21 build
logs would have been lost.

**A blind instrument, flagged.** `git diff HEAD -- <those logs>` reports
UNCHANGED for all seven — because everything under `results/` is **untracked**, so
git cannot see edits to it at all. A `git`-based "committed evidence untouched"
check is worthless for this evidence store, and the comparison must be against a
preserved copy. This is the same family as G5-F3, where an overwritten BASELINE
log left the `4 -> 0` claim without a supporting log. Recorded as a standing
hazard for G8–G10, which run more frozen scripts that write into their own
committed log paths.

## 11. Step 12 — source fingerprint

```text
V2 before G7   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 after  G7   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

Equal. Components unchanged: tracked diff `2f71bb14…`, untracked engineering
`750012b4…`, solver-output sub-hash `3bf622aa…`, 112 tracked and 122 untracked
files. G7 required no source or harness edit.

## 12. What this gate does not claim

Carried forward verbatim from the frozen W statements, none of them softened:

* **Native Windows package — NOT CLAIMED.** Nothing was built or run on Windows.
  The 0.1.0 and v0.2.0 Windows evidence is cited, never re-asserted. The naming
  function proves the *derivation*, which is not the same as a package.
* **DEB / RPM — NOT CLAIMED.** CPack offers them on this host; they are not
  enabled, and an untested `.deb` is exactly the untested platform support the
  brief forbids claiming.
* **macOS — NOT CLAIMED.** The naming function answers for `Darwin` so the
  decision is total rather than silently falling through, but nothing was built
  or run there.
* **No library/header install and no CMake package config.** Recorded as debt.
* `CFDAPP_ENABLE_CUDA=OFF` for both packaging configurations, as the frozen
  instrument specifies. This gate qualifies packaging, not the CUDA build.
* `final.sh`'s three-configuration full regression was not run here; it is G10's
  gate. Only its `CFDAppPackageNaming` ctests were run.
