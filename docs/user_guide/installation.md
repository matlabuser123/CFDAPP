# Installation

## Building from source

CFDApp is a standard CMake C++20 project.

```bash
cmake -S CFDApp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces the CLI, `build/apps/cli/cfdapp`, and needs nothing beyond
a C++20 compiler, CMake, and the dependencies CMake fetches itself
(GoogleTest, nlohmann/json) -- no Qt, no CUDA, no GPU. This is the build
every CI job and every numerical/validation test in this repository
runs against.

### Optional: the GUI (`cfdapp_gui`)

```bash
cmake -S CFDApp -B build -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=ON
cmake --build build -j
```

Requires a Qt 6 development install providing the `Core`, `Gui`, `Qml`,
`Quick`, and `QuickControls2` components (Qt 6.2+; developed and tested
against Qt 6.2.4). On Debian/Ubuntu:

```bash
sudo apt install qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-templates qml6-module-qtquick-window \
  qml6-module-qtquick-layouts qml6-module-qtqml-workerscript
```

If Qt isn't found, `-DCFDAPP_BUILD_GUI=ON` prints a warning and skips
`cfdapp_gui` -- the CLI and every other target build normally either
way (the GUI is never required, see
[getting_started.md](getting_started.md)).

### Optional: OpenMP / CUDA

`-DCFDAPP_ENABLE_OPENMP=ON` and `-DCFDAPP_ENABLE_CUDA=ON` enable the
performance-phase parallel/GPU paths (see the repository's own P4
status notes for what those actually accelerate and their measured
results) -- neither is needed for ordinary use.

## Running the tests

```bash
cd build && ctest -j8
```

## Packaging (Windows)

A CMake/CPack packaging configuration (`cmake/Packaging.cmake`)
produces a portable ZIP and an NSIS installer for a GUI-enabled Release
build:

```powershell
cmake -S CFDApp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCFDAPP_BUILD_GUI=ON -DCFDAPP_ENABLE_PACKAGING=ON `
  -DCMAKE_PREFIX_PATH=<path to your Qt6 install>
cmake --build build -j
cd build && cpack -G "ZIP;NSIS"
```

Built, packaged, and smoke-tested (including the packaged GUI, not just
the CLI) on a real Windows machine -- see
[../developer_guide/packaging.md](../developer_guide/packaging.md) and
`results/release/0.1.0/` for the full evidence, and
`scripts/windows-release/` for the exact, verified step-by-step recipe.
The one remaining gap is the actual GitHub Actions release workflow,
which requires a git remote and CI access this repository doesn't have
configured here -- see that same doc.
