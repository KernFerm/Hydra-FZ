$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if ($compiler) {
    & gcc -std=c11 -Wall -Wextra -Werror "$PSScriptRoot\engine_test.c" "$root\hydra_engine.c" -o "$env:TEMP\hydra_engine_test.exe"
    if ($LASTEXITCODE -ne 0) { throw "C engine compilation failed" }
    & "$env:TEMP\hydra_engine_test.exe"
    if ($LASTEXITCODE -ne 0) { throw "C engine test failed" }
} else {
    Write-Host "gcc unavailable; target build compiles the C engine"
}
python -m unittest discover -s "$PSScriptRoot" -p "test_companion.py"
if ($LASTEXITCODE -ne 0) { throw "Companion tests failed" }
python -m py_compile "$root\companion\hydra_fz_bridge.py"
if ($LASTEXITCODE -ne 0) { throw "Companion syntax check failed" }
Write-Host "Hydra FZ host tests passed"
