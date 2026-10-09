# P13-006 — Packaging: audit

Performed on the tree that closed P13-005, before any change. Everything below was **measured on
this host**, not read out of the configuration.

```text
environment  WSL2 Ubuntu 22.04, gcc 11.4, CMake + CPack, Qt6 available, no makensis
probes       tools/audit_probe.sh, audit_probe2.sh, audit_probe3.sh
logs         logs/00_configure_gui_packaging.log .. 05_installed_run.log
```

---

## What already exists, and is not being redone

Packaging was built, packaged and smoke-tested **on a real Windows machine**, twice — for 0.1.0
(`results/release/0.1.0/`) and for v0.2.0 (`results/release/v0.2.0/`), including a clean-machine
test that launched the packaged executables from a process whose `PATH` had never seen the
developer's Qt. That evidence stands. This item does not re-run it, does not re-claim it, and must
not break it: `.github/workflows/release.yml` globs `build/CFDApp-*-Windows-x64.zip` and
`...-Windows-x64.exe`, so the Windows artifact name is a contract.

---

## F1 — a Linux build produces a package that says it is Windows

```text
$ cd build/pkg && cpack
CPack: - package: .../build/pkg/CFDApp-0.2.0-Windows-x64.zip generated.

$ file CFDApp-0.2.0-Windows-x64/bin/cfdapp
ELF 64-bit LSB pie executable, x86-64, ... for GNU/Linux 3.2.0
```

The name is hardcoded:

```cmake
set(CPACK_PACKAGE_FILE_NAME "CFDApp-${CFDApp_VERSION}-Windows-x64")
```

A 1.9 MB archive of Linux ELF binaries, labelled `Windows-x64`. Not a gap — a package that
misstates its own platform, which is the one thing a package name is for.

## F2 — the package version and the binary inside it disagree

```text
project(CFDApp VERSION 0.2.0)
cfdapp --version                CFDApp 0.2.0-dev
CPACK_PACKAGE_VERSION           0.2.0
package file name               CFDApp-0.2.0-Windows-x64.zip
packaged cfdapp --version       CFDApp 0.2.0-dev
```

`CPACK_PACKAGE_VERSION` is assembled from `CFDApp_VERSION_{MAJOR,MINOR,PATCH}` and silently drops
`CFDAPP_VERSION_SUFFIX`, which `cfdapp --version` includes. So a package built from a development
tree is named **exactly** like the released 0.2.0, while the binary inside it says `0.2.0-dev`.

`Packaging.cmake`'s own comment claims *"one authoritative version source ... never a second,
separately-maintained packaging-only version string."* It has a second one anyway, by omission.

This is not a defect of the release path: `release.yml` configures with
`-DCFDAPP_VERSION_SUFFIX=""`, so a tagged build's suffix is empty and the two agree. It is a defect
of **every other** packaging build — which is precisely the build a person is most likely to hand
someone informally.

Same principle P13-004 already settled for `build_identity`: a package that claims a release the
binary does not match is worse than one that admits what it is.

## F3 — `cpack` fails on any host without NSIS, *after* producing an artifact

```text
CPack: - package: .../CFDApp-0.2.0-Windows-x64.zip generated.
CPack Error: Cannot find NSIS compiler makensis: likely it is not installed...
CPack Error: Cannot initialize the generator NSIS
cpack exit status: 1
```

`set(CPACK_GENERATOR "ZIP;NSIS")` is unconditional. The combination is the bad one: the ZIP
generator runs first and succeeds, then NSIS fails the command. A script that checks the exit
status aborts *after* an artifact has been written; one that does not ships the mislabelled zip
from F1.

## F4 — a CLI-only package is refused outright

```cmake
if(NOT CFDAPP_BUILD_GUI)
  message(WARNING "... packaging is only meaningful for the GUI build. Skipping.")
  return()
endif()
```

Measured: `-DCFDAPP_BUILD_GUI=OFF -DCFDAPP_ENABLE_PACKAGING=ON` configures with exit 0 and produces
**no `CPackConfig.cmake` at all**. The headless configuration — a cluster, a container, a CI image,
anywhere without Qt — is the one that cannot be packaged.

The comment's reasoning ("a CLI-only package would just be the plain `cfdapp` executable, which
needs none of this") is not what the install rules say: examples, user docs and the licence are
installed independently of the GUI, and a CLI user wants all three.

## F5 — what is already right (PRESERVATION — measured, not assumed)

```text
cmake --install build/pkg --prefix /tmp/p13_pkg_install
  exit status              0
  files installed          125          total 5.2M
  top level                bin docs examples licenses
  bin/                     cfdapp  cfdapp_gui          (exactly 2 entries)
  build-only artifacts     none        (no CMakeFiles, *_autogen, *.o, *.a,
                                        CMakeCache.txt, test binaries)
  installed cfdapp runs an installed example    exit 0
```

The `install(DIRECTORY)` exclusion patterns are written for Windows (`PATTERN "cfdapp_gui.exe"
EXCLUDE`) and match nothing here, yet nothing leaked: on Linux the excluded `CMakeFiles`,
`*_autogen` and `CFDGuiControllerTests*` patterns still catch the build-only content, and the two
binaries are installed to the same destination by both rules, so the result is one copy each. That
is luck rather than design, but it is measured, and it is the baseline any change must preserve.

## F6 — no library or header install, and no CMake package config

```text
install(TARGETS ...) rules in cmake/Packaging.cmake     2  (both executables)
library / header install rules                          0
install(EXPORT ...) / CMakePackageConfigHelpers          0
```

CFDApp cannot be consumed as a library by another CMake project. Recorded as debt; not in this
item's scope, which is about the *application* package being correct about itself.

---

## Dry-run record, measured on the unchanged tree

```text
F1  package name on Linux                CFDApp-0.2.0-Windows-x64.zip   target: names the real platform
F2  package version vs binary version    0.2.0  vs  0.2.0-dev           target: equal
F3  cpack exit status on this host       1                              target: 0
F4  CPackConfig.cmake with GUI=OFF       not produced                   target: produced
F5  install tree cleanliness             clean, 125 files, bin/ = 2     PRESERVATION
F5  installed cfdapp runs an example     exit 0                         PRESERVATION
```

Vacuity check: F1–F4 all have measured baselines that **differ** from their targets, so each is a
real detection. F5 is PRESERVATION and already passes, which is correct for that kind and is not
counted as an achievement.

## What the audit rules out

* **No Windows package build, and no Windows claim.** This host cannot produce or test one. The
  existing Windows evidence is cited, never re-asserted.
* **No DEB/RPM.** CPack on this host offers them, but an untested `.deb` is exactly the
  "package/platform support that was not tested" the brief forbids claiming.
* **No change to the install layout.** F5 says it is already correct.
