# P5 -- Final Release Gate, sections 9-10: run CPack (ZIP + NSIS) on the
# already-built Release tree and list what it produced.

$ErrorActionPreference = "Stop"
$build = "C:\Users\Hasib\Desktop\CFDAPP\CFDApp\build\windows-release"
$cmakeBin = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$env:PATH = "$cmakeBin;C:\Program Files (x86)\NSIS;$env:PATH"

Push-Location $build
cpack -C Release -G "ZIP;NSIS"
$cpackExit = $LASTEXITCODE
Pop-Location

Write-Output "=== cpack exit code: $cpackExit ==="
Write-Output "=== produced files ==="
Get-ChildItem "$build\CFDApp-*" -ErrorAction SilentlyContinue | Select-Object Name, Length
