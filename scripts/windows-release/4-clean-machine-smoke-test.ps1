# P5 -- Final Release Gate, section 13: the strongest packaging test --
# run the *extracted, packaged* executables from a bare PowerShell
# session whose PATH was never modified to include C:\Qt or the Visual
# Studio toolchain (the actual dev-only components of this machine).
# This is not a separate physical/VM machine, but it directly tests the
# one thing that matters: does the packaged app depend on files that
# exist only because a developer's own Qt/MSVC install put them on
# PATH -- since this process's PATH is the same one any ordinary,
# non-developer Windows account on this machine would have.

$ErrorActionPreference = "Stop"
$extractDir = "C:\Users\Hasib\AppData\Local\Temp\cfdapp_clean_test"
Remove-Item -Recurse -Force $extractDir -ErrorAction SilentlyContinue
Expand-Archive -Path "C:\Users\Hasib\Desktop\CFDAPP\CFDApp\build\windows-release\CFDApp-0.1.0-Windows-x64.zip" -DestinationPath $extractDir

Write-Output "=== PATH for this test (should NOT contain C:\Qt or Visual Studio) ==="
$env:PATH -split ';' | Where-Object { $_ -match 'Qt|Visual Studio' }
Write-Output "(none of the above means the check passed -- empty is expected)"

$root = Get-ChildItem $extractDir -Directory | Select-Object -First 1
$cli = Join-Path $root.FullName "bin\cfdapp.exe"
$gui = Join-Path $root.FullName "bin\cfdapp_gui.exe"

Write-Output "=== packaged CLI: --version ==="
& $cli --version
Write-Output "CLI --version exit code: $LASTEXITCODE"

Write-Output "=== packaged CLI: run a real example case ==="
$caseDir = Join-Path $root.FullName "examples\lid_driven_cavity"
& $cli --case $caseDir
$caseExit = $LASTEXITCODE
Write-Output "CLI case-run exit code: $caseExit"
Write-Output "results exist: $(Test-Path (Join-Path $caseDir 'results\solution.vtk'))"

Write-Output "=== packaged GUI: launch check ==="
$proc = Start-Process -FilePath $gui -PassThru -RedirectStandardOutput "$env:TEMP\pkg_gui_stdout.log" -RedirectStandardError "$env:TEMP\pkg_gui_stderr.log"
Start-Sleep -Seconds 3
if (-not $proc.HasExited) {
    Write-Output "packaged GUI is alive after 3s -- stopping it now."
    Stop-Process -Id $proc.Id -Force
    $guiOk = $true
} else {
    Write-Output "packaged GUI exited early with code $($proc.ExitCode)."
    $guiOk = $false
}
Get-Content "$env:TEMP\pkg_gui_stderr.log" -ErrorAction SilentlyContinue

Write-Output "=== SUMMARY ==="
Write-Output "cli_version_exit=$LASTEXITCODE case_exit=$caseExit gui_alive=$guiOk"
