# Packaging and release automation (P5-J/K) -- status

**Packaging is built, packaged, and smoke-tested on a real Windows
machine, with real evidence saved under `results/release/0.1.0/`.**
Release *automation* -- the actual `.github/workflows/release.yml` run
-- has not: this repository has no git remote configured and no `gh`
CLI available in this environment, so there was no way to push a tag
and trigger it, or inspect the result, from here. See
`results/release/0.1.0/release_summary.json` for the complete,
itemized breakdown of what was and wasn't verified, and how.

## How to build/package it yourself (the exact, verified recipe)

`scripts/windows-release/` -- run in order, on a real Windows machine
with Visual Studio (MSVC + its own bundled CMake/Ninja) installed:

1. `1-configure-and-build.ps1` -- installs nothing itself; imports the
   MSVC x64 dev environment, then configures
   (`-DCFDAPP_BUILD_GUI=ON -DCFDAPP_ENABLE_PACKAGING=ON
   -DCMAKE_PREFIX_PATH=<your Qt kit>`) and builds
   `build/windows-release`. Edit the `$qtDir` variable at the top to
   point at your own Qt install.
2. `2-test-and-smoke.ps1` -- full `ctest`, then launches the build-tree
   `cfdapp_gui.exe` (real "windows" platform plugin) and runs the
   build-tree `cfdapp.exe` against a real example case.
3. `3-package.ps1` -- runs `cpack -G "ZIP;NSIS"`.
4. `4-clean-machine-smoke-test.ps1` -- extracts the ZIP and launches
   the *packaged* executables from a process whose `PATH` was never
   given the developer's own Qt/Visual-Studio directories, then runs a
   real case from the packaged, extracted location.

Qt: install via [aqtinstall](https://github.com/miurahr/aqtinstall)
(`pip install aqtinstall`, then `aqt install-qt windows desktop 6.9.3
win64_msvc2022_64`) or the official Qt online installer. NSIS: `winget
install NSIS.NSIS` (only needed for the installer, not the ZIP).

## What's actually in the package

`cmake/Packaging.cmake`'s `install()` rules produce, under one
top-level `CFDApp-<version>-Windows-x64/` directory:

```text
bin/       cfdapp.exe, cfdapp_gui.exe, every Qt DLL and plugin
           subdirectory (platforms/, qml/, iconengines/, imageformats/,
           networkinformation/, tls/, translations/) windeployqt
           deployed, plus the VC++ redistributable installer
examples/  every case under cases/ (minus any results/ subdirectory)
docs/      docs/user_guide/
licenses/  LICENSE
```

Two generators, from the one shared `install()` tree: `CFDApp-<version>
-Windows-x64.zip` (portable) and `CFDApp-<version>-Windows-x64.exe` (an
NSIS installer -- note: no `-Setup` suffix; `CPACK_PACKAGE_FILE_NAME` is
shared by both generators and CPack only appends the extension).

**A genuine defect was found and fixed here**: the first package built
during this task was only ~440KB (two bare `.exe` files, no Qt runtime
at all) -- `install(TARGETS cfdapp cfdapp_gui ...)` only ever packages
the executables themselves, never the DLLs/plugins a separate
`windeployqt` POST_BUILD custom command drops next to them in the build
tree. Fixed with an explicit `install(DIRECTORY
$<TARGET_FILE_DIR:cfdapp_gui>/ DESTINATION bin ...)` rule (excluding the
build directory's own intermediate artifacts and the
`CFDGuiControllerTests` test binary, which happens to share that same
output directory). The corrected package is ~50-61MB and was confirmed,
by direct execution, to run standalone (see below).

A second genuine defect, upstream of packaging: `cmake/QtDeploy.cmake`'s
`add_custom_command(TARGET cfdapp_gui POST_BUILD ...)` was originally
`include()`'d from `cmake/Packaging.cmake` (root-CMakeLists.txt scope)
-- CMake requires that call to happen in the *same directory scope*
that created the target, so this failed configure outright on the first
real Windows attempt (`TARGET 'cfdapp_gui' was not created in this
directory`) despite configuring "successfully" (by short-circuiting
before ever reaching that line) on Linux. Fixed by moving the
`include(cmake/QtDeploy.cmake)` into `apps/gui/CMakeLists.txt` itself,
right after the `cfdapp_gui` target is created.

A third: Qt 6.5.3's `win64_msvc2019_64` kit (the original plan) fails to
compile against this machine's MSVC 19.51 (Visual Studio 2026) toolset
-- `error C3861: 'stdext': identifier not found` in Qt's own
`qvarlengtharray.h`, a genuine Qt-version/MSVC-toolset incompatibility.
Fixed by using Qt 6.9.3's `win64_msvc2022_64` kit instead (built against
a newer, compatible MSVC baseline) -- both locally and in
`release.yml`.

## Verified, with real evidence (`results/release/0.1.0/`)

- Windows Release build (MSVC 19.51.36256.0, Qt 6.9.3): **PASS**
- Full `ctest`: **1162/1162 (100%)** -- see `ctest.txt`. (One transient
  parallel-execution race on a shared test-fixture directory was seen
  and disclosed in `release_summary.json`; it is not a logic defect --
  confirmed by two clean full-suite reruns.)
- ZIP + NSIS installer both build: **PASS**
- Qt runtime actually deployed into the package: **PASS** (after the
  fix above)
- Packaged CLI (`--version`, and a full `lid_driven_cavity` run from
  the extracted package): **PASS**
- Packaged GUI (launched from the extracted package, stayed alive,
  real "windows" platform plugin): **PASS**
- Clean-machine dependency approximation (packaged exes launched from a
  process whose `PATH` excludes this machine's own Qt/Visual Studio
  install): **PASS** -- see `release_summary.json` for exactly what
  this does and doesn't prove versus a literal separate VM.
- CPU-only fallback: **PASS**, trivially (CUDA was never enabled for
  this build).
- Version metadata consistency (`cfdapp --version`, GUI title/About
  dialog, package filenames): **PASS**, after adding the GUI's
  previously-nonexistent version display
  (`SimulationController::applicationVersion`/`applicationName`, a
  window-title binding, and an About dialog in `Main.qml`).
- SHA256 checksums: **PASS** -- `SHA256SUMS.txt`.

## What's still not done

The actual `.github/workflows/release.yml` execution, and retesting a
downloaded release artifact -- both require pushing a tag to a GitHub
remote this local repository does not have configured. `release.yml`
was corrected to match everything verified above (the working Qt
version/kit, the real NSIS output filename, and a packaged-GUI
smoke-test step it was missing) so it reflects the best available,
locally-proven recipe -- but it remains genuinely unexecuted. Whoever
next has push/CI access should trigger it once (a `v0.1.0`-style tag)
and confirm it end-to-end before treating Release Automation (P5-K,
distinct from Packaging, P5-J) as complete.
