# P13-006 — Packaging: summary

**A Linux build produced `CFDApp-0.2.0-Windows-x64.zip` containing Linux ELF binaries, and `cpack`
exited 1 after writing it.** Four measured defects, all closed, with the verified Windows artifact
name left byte-identical.

```text
gate        results/p13/packaging/acceptance_gate.md   (frozen before any change)
audit       results/p13/packaging/audit.md
baseline    the tree that closed P13-005
environment WSL2 Ubuntu 22.04, gcc 11.4, CMake/CPack, Qt6, no makensis
```

---

## 1. What was wrong, measured

```text
F1  a Linux build's package name     CFDApp-0.2.0-Windows-x64.zip
    what was inside it               ELF 64-bit LSB pie executable ... for GNU/Linux 3.2.0
F2  package version / binary version 0.2.0  vs  0.2.0-dev
F3  cpack exit status on this host   1        (unconditional NSIS, after ZIP had succeeded)
F4  CLI-only packaging               no CPackConfig.cmake produced at all
```

**F1** is the one that matters most: a package name exists to say what the package is, and this one
said Windows about 1.9 MB of Linux binaries.

**F2** is subtler and worse in one way. `CPACK_PACKAGE_VERSION` was assembled from
major/minor/patch and dropped `CFDAPP_VERSION_SUFFIX`, which `cfdapp --version` includes — so a
package built from any development tree was named **exactly** like the released 0.2.0 while the
binary inside said `0.2.0-dev`. `Packaging.cmake`'s own comment claimed *"one authoritative version
source ... never a second, separately-maintained packaging-only version string."* It had a second
one anyway, by omission. This is the same principle P13-004 settled for `build_identity`: a package
claiming a release the binary does not match is worse than one that admits what it is.

**F3** failed in the worst order: ZIP ran first and succeeded, *then* NSIS failed the command. A
release script that checks the exit status aborts holding a finished artifact; one that does not
ships the mislabelled zip from F1.

**F4**: `-DCFDAPP_BUILD_GUI=OFF -DCFDAPP_ENABLE_PACKAGING=ON` configured with exit 0 and produced
nothing. The headless configuration — a cluster, a container, a CI image — was the one that could
not be packaged.

## 2. The fix, and why it is a function

The name and generator list now come from one pure function,
`cfdapp_package_naming()` in `cmake/PackageNaming.cmake`, which takes the system as an **argument**.

That is not decoration. The Windows artifact name is a **contract**:
`.github/workflows/release.yml` globs `build/CFDApp-*-Windows-x64.zip` and `...-Windows-x64.exe`,
and the Windows packaging was built, packaged and clean-machine smoke-tested on real hardware for
0.1.0 and v0.2.0. No Windows machine is available to routine testing here — but a pure function of
the system name can be driven for Windows from any host with `cmake -P`. So the contract is
**asserted as a test** rather than argued:

```text
windows release            CFDApp-0.2.0-Windows-x64       [ZIP;NSIS]   <- release.yml's glob
windows x86_64 spelling    CFDApp-0.2.0-Windows-x64       [ZIP;NSIS]
windows dev                CFDApp-0.2.0-dev-Windows-x64   [ZIP;NSIS]
linux release              CFDApp-0.2.0-Linux-x64         [TGZ;STGZ]
linux arm64                CFDApp-0.2.0-Linux-arm64       [TGZ;STGZ]
macos arm64                CFDApp-0.2.0-macOS-arm64       [TGZ]
NSIS requested on Windows only                            ok
an unsupported system                                     REFUSED, exit 1
```

### 2.1 The function refuses rather than improvises

An unrecognised system is a `FATAL_ERROR`, not a fallback. `CFDApp-0.2.0-Haiku-x64.tar.gz` would be
a claim that CFDApp runs on Haiku, and nobody has built or run it there. A package name is a claim,
and the rule this item is under is not to claim a platform that was not tested.

### 2.2 A bug the test found in its first run

The argument check was written `if(NOT PN_${required})`. CMake's `if(<variable>)` evaluates the
variable's **value** against its false constants — which include `N`, `NO`, `OFF` and `IGNORE`. So
`cfdapp_package_naming(... OUT_NAME n ...)` was rejected as *"OUT_NAME is required"* purely because
the output variable was named `n`. Fixed with `DEFINED` + `STREQUAL ""`, and the test now calls the
function with output variables named `n`, `no`, `off` and `ignore`.

## 3. Results

```text
N1   a Linux build is named Linux                      PASS  CFDApp-0.2.0-dev-Linux-x64.tar.gz
N2   the Windows name is unchanged                     PASS  asserted via cmake -P
N3   the package version is the binary's version       PASS  0.2.0-dev in both
N4   packaged binary type matches the name             PASS  ELF/GNU-Linux in a Linux package
G1   cpack exit status                                 PASS  0  (was 1)
G2   NSIS requested only on Windows                    PASS  never mentioned on Linux
G3   Windows generators still exactly ZIP;NSIS         PASS  asserted via cmake -P
C1   CLI-only: CPackConfig produced, cpack exit 0      PASS
C2   CLI package has no cfdapp_gui; has examples,
     docs and licenses                                 PASS
C3   GUI package still has both executables            PASS
P1   install trees clean, no build-only artifacts      PASS  125 and 124 files
P2   the PACKAGED cfdapp ran a PACKAGED example        PASS  exit 0, "Converged: yes"
P3   examples/, docs/, licenses/ present               PASS
```

P2 is the one worth naming: that is the binary extracted from the tarball, running a case from the
packaged `examples/`, with a working directory outside the tree — not the build-tree binary.

Non-vacuity controls — each must fail, and each did:

```text
NV1  hardcode the Windows name again   -> N1, and the naming test    DETECTED
NV2  drop the version suffix           -> N3 (claims the release)    DETECTED
NV3  force NSIS into Linux generators  -> G1, cpack exit 1           DETECTED
NV4  restore the GUI-only guard        -> C1, no CPackConfig.cmake   DETECTED
NV5  an unknown system                 -> refused, exit 1            DETECTED
NV6  cleanliness check vs a build tree -> 457 artifacts found        DETECTED
```

Both CMake files were restored from a pre-control copy with the restore **sha256-verified**, and
the restored tree reconfigures to `CFDApp-0.2.0-dev-Linux-x64 (TGZ;STGZ)`.

## 4. Permanent tests

```text
CFDAppPackageNaming                       tests/cmake/test_package_naming.cmake
CFDAppPackageNamingRefusesUnknownSystem   tests/cmake/test_package_naming_refuses.cmake (WILL_FAIL)
```

Both run via `cmake -P` without configuring or building anything, so they cost milliseconds and are
not tied to `CFDAPP_ENABLE_PACKAGING` being on. The refusal needs its own process because a CMake
script cannot catch its own `FATAL_ERROR`.

## 5. Regression (criteria P4, P5)

```text
                      ctest exit   tests passed   failed
Release + CUDA                 0           2048        0
Debug + GUI                    0           2042        0
CPU-only                       0           1982        0
clang-format-18, CI scope                     0 violations
```

Exit status read from ctest itself, not from a `| tail -N; echo RC=$?` pipeline (that reports
`tail`'s status — the flaw corrected during P13-005). Both new tests ran in **all three** suites:

```text
Test #17: CFDAppPackageNaming ........................ Passed
Test #18: CFDAppPackageNamingRefusesUnknownSystem .... Passed
```

P5 is covered by the same three runs: all three are default, packaging-**OFF** builds, and they
configure and pass exactly as before. The packaging change reaches them only through the two new
`cmake -P` tests, which need neither packaging nor a build.

## 5. What this item deliberately does NOT claim

* **No Windows package was built, tested or claimed here.** This host cannot produce one. The
  0.1.0 and v0.2.0 Windows evidence (`results/release/`) is **cited**, never re-asserted, and the
  change is constructed so that the verified name and generator list are what a Windows build still
  produces.
* **No DEB and no RPM.** CPack offers both on this host. Neither has been installed or run
  anywhere, and an untested `.deb` is exactly the "package/platform support that was not tested"
  the brief forbids claiming. Linux gets TGZ (which preserves the executable bit and symlinks,
  unlike ZIP) and STGZ.
* **No macOS claim.** `Darwin` is handled so the decision is total rather than falling through
  silently, and it yields a plain TGZ — no `.dmg`, no bundle, no signing or notarization. Nothing
  has been built or run there.
* **No library or header install, and no CMake package config** (`audit.md` F6: 0 rules, 0
  exports). CFDApp still cannot be consumed as a library by another CMake project. Recorded as
  debt; this item is about the application package being correct about itself.
* **No change to the install layout.** It was measured clean before this item and had to stay that
  way — criterion P1, not an improvement.
