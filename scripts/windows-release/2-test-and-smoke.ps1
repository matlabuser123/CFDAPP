# P5 -- Final Release Gate, sections 6/7/9/10: run the full Windows
# Release ctest suite, then a headless-ish GUI startup check, on the
# *build-tree* executables (before packaging) -- section 6/7's own
# pre-packaging gates.

$ErrorActionPreference = "Stop"
$build = "C:\Users\Hasib\Desktop\CFDAPP\CFDApp\build\windows-release"
$cmakeBin = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$qtBin = "C:\Qt\6.9.3\msvc2022_64\bin"
# CFDGuiControllerTests.exe (unlike cfdapp_gui.exe) has no windeployqt-
# bundled copy of Qt's own DLLs next to it (the POST_BUILD deploy step
# only targets cfdapp_gui, see apps/gui/CMakeLists.txt) -- ctest-launched
# test binaries need Qt's bin directory on PATH to find Qt6Core.dll/
# Qt6Test.dll at all, the same way a real Windows CI's Qt-install step
# (e.g. jurplel/install-qt-action) already does automatically.
$env:PATH = "$cmakeBin;$qtBin;$env:PATH"

Write-Output "=== ctest ==="
Push-Location $build
ctest --output-on-failure -j 4
$ctestExit = $LASTEXITCODE
Pop-Location
Write-Output "ctest exit code: $ctestExit"

Write-Output "=== GUI startup check (build tree) ==="
# The native "windows" QPA platform plugin (the only one windeployqt
# bundles by default -- "offscreen" is a separate opt-in module this
# project does not currently request) -- a real Windows machine can
# create real (even if never shown on screen in this non-interactive
# session) windows via it, so no override is needed or wanted here.
$guiExe = "$build\apps\gui\cfdapp_gui.exe"
$proc = Start-Process -FilePath $guiExe -PassThru -RedirectStandardOutput "$env:TEMP\gui_stdout.log" -RedirectStandardError "$env:TEMP\gui_stderr.log"
Start-Sleep -Seconds 3
if (-not $proc.HasExited) {
    Write-Output "GUI process is alive after 3s (event loop running) -- stopping it now."
    Stop-Process -Id $proc.Id -Force
    $guiOk = $true
} else {
    Write-Output "GUI process exited early with code $($proc.ExitCode) -- unexpected."
    $guiOk = $false
}
Write-Output "--- stdout ---"
Get-Content "$env:TEMP\gui_stdout.log" -ErrorAction SilentlyContinue
Write-Output "--- stderr ---"
Get-Content "$env:TEMP\gui_stderr.log" -ErrorAction SilentlyContinue

Write-Output "=== CLI version/case check (build tree) ==="
$cliExe = "$build\apps\cli\cfdapp.exe"
& $cliExe --version
& $cliExe --case "C:\Users\Hasib\Desktop\CFDAPP\CFDApp\cases\lid_driven_cavity"
$cliExit = $LASTEXITCODE
Write-Output "CLI exit code: $cliExit"

Write-Output "=== SUMMARY ==="
Write-Output "ctest: $ctestExit  gui_alive: $guiOk  cli: $cliExit"
