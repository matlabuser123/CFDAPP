# P5 -- Final Release Gate: one-shot Windows Release configure+build
# script. Imports the MSVC x64 dev environment (vcvars64.bat) into this
# PowerShell process, then puts CMake/Ninja (bundled with Visual
# Studio) and Qt6 on PATH, then configures and builds
# build/windows-release with CFDAPP_BUILD_GUI + CFDAPP_ENABLE_PACKAGING
# both ON.

$ErrorActionPreference = "Stop"

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmakeBin = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBin = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$qtDir = "C:\Qt\6.9.3\msvc2022_64"

# Import vcvars64.bat's own environment variables into this process --
# the standard cmd.exe "call vcvars then dump env" trick, since
# PowerShell cannot source a .bat file directly.
$envDump = cmd /c "`"$vcvars`" && set"
foreach ($line in $envDump) {
    if ($line -match "^([^=]+)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
    }
}

$env:PATH = "$cmakeBin;$ninjaBin;$qtDir\bin;$env:PATH"

Write-Output "=== cmake ==="
cmake --version
Write-Output "=== ninja ==="
ninja --version
Write-Output "=== qmake ==="
qmake --version

$src = "C:\Users\Hasib\Desktop\CFDAPP\CFDApp"
$build = "$src\build\windows-release"

# CFDAPP_VERSION_SUFFIX="" (root CMakeLists.txt's own comment: "Leave
# empty for a tagged release build") -- without this, a real release
# build still reports itself as e.g. "0.2.0-dev" (the CACHE default),
# wrong for what actually ships in the package (see .github/workflows/
# release.yml's own comment on this exact gap, previously present here
# too).
& cmake -S $src -B $build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCFDAPP_BUILD_GUI=ON `
    -DCFDAPP_ENABLE_PACKAGING=ON `
    -DCFDAPP_VERSION_SUFFIX="" `
    -DCMAKE_PREFIX_PATH="$qtDir"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& cmake --build $build -j
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

Write-Output "=== BUILD SUCCEEDED ==="
