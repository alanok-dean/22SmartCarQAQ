# build.ps1 -- one-command build / flash for the M4-1 Keil project
#
# Why this script:
#   Clicking buttons in the Keil GUI cannot be reproduced from a shell, cannot be
#   scripted, and cannot be handed to a teammate. UV4.exe accepts command line
#   arguments, so a reproducible build and flash is possible.
#
# Usage (run from this directory):
#   powershell -ExecutionPolicy Bypass -File .\build.ps1
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Rebuild
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Flash
#
# NOTE: ASCII-only output on purpose. Windows PowerShell 5.1 reads a UTF-8 file
# without BOM as ANSI/GBK, which corrupts non-ASCII string literals and breaks
# parsing. Keeping the script ASCII avoids that class of failure entirely.
#
# NOTE: close the Keil GUI before running. Both would touch the same .uvprojx.
#
# KNOWN LIMITATION -- flashing from the command line does not work reliably here.
#   Symptom: "ST-LINK USB communication error / Target DLL has been cancelled".
#   Cause:   this board's ST-Link only handshakes in "Connect under Reset" mode,
#            and the board's 4-pin debug header has no NRST line. The working
#            procedure in the GUI is: hold the reset button, click Connect,
#            release. A command line session cannot inject that timing.
#   Decision: -Flash is kept in the script for completeness, but the supported
#            workflow is: build here, flash with Keil GUI (F8).
#            Everything up to the binary is reproducible from the shell.

param(
    [switch]$Rebuild,
    [switch]$Flash
)

$ErrorActionPreference = 'Stop'

$UV4  = 'D:\Keil\UV4\UV4.exe'
$proj = Join-Path $PSScriptRoot '..\MDK-ARM\M4_1_LED_LIGHTEN.uvprojx'
$log  = Join-Path $PSScriptRoot 'build.log'

if (-not (Test-Path $UV4))  { Write-Host "[ERR] not found: $UV4"  -ForegroundColor Red; exit 1 }
if (-not (Test-Path $proj)) { Write-Host "[ERR] not found: $proj" -ForegroundColor Red; exit 1 }

if (Get-Process -Name UV4 -ErrorAction SilentlyContinue) {
    Write-Host '[ERR] Keil GUI is running - close it first (it would overwrite .uvprojx)' -ForegroundColor Red
    exit 1
}

$mode = if ($Rebuild) { '-r' } else { '-b' }
Write-Host "[BUILD] mode=$mode project=$proj" -ForegroundColor Cyan
& $UV4 $mode $proj -j0 -o $log | Out-Null
$code = $LASTEXITCODE

if (Test-Path $log) {
    Get-Content $log -Tail 10 | ForEach-Object { Write-Host "  $_" }
}

switch ($code) {
    0       { Write-Host '[OK] build passed: 0 error, 0 warning' -ForegroundColor Green }
    1       { Write-Host '[OK] build passed with warnings'       -ForegroundColor Yellow }
    2       { Write-Host '[FAIL] compile errors - see build.log' -ForegroundColor Red; exit 2 }
    3       { Write-Host '[FAIL] fatal error (project locked?)'  -ForegroundColor Red; exit 3 }
    default { Write-Host "[FAIL] UV4 exit code $code"           -ForegroundColor Red; exit $code }
}

if ($Flash) {
    $flog = Join-Path $PSScriptRoot 'flash.log'
    Write-Host '[FLASH] downloading via ST-Link' -ForegroundColor Cyan
    & $UV4 -f $proj -j0 -o $flog | Out-Null
    $fc = $LASTEXITCODE
    if ($fc -eq 0) {
        Write-Host '[OK] flash done - press RST on the board (Keil does not auto-run by default)' -ForegroundColor Green
    } else {
        Write-Host "[FAIL] flash exit code $fc - KNOWN LIMITATION, see header comment." -ForegroundColor Red
        Write-Host '       Flash from the Keil GUI (F8) instead: this board needs' -ForegroundColor Yellow
        Write-Host '       "Under Reset" + a manual reset pulse that a CLI cannot inject.' -ForegroundColor Yellow
        exit $fc
    }
}
