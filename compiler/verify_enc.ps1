# ===========================================================================
#  Ground truth for the Wandaa instruction encoder, on Windows.
#
#  The PowerShell twin of compiler/verify_enc.sh, for the CI job that runs
#  native .exe files rather than wine. Generates the shared cases from the
#  C++ encoder, encodes them again with compiler/x64.waa, and requires the
#  two byte streams to be identical.
# ===========================================================================
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$wandaac = if ($env:WANDAAC) { $env:WANDAAC } else { ".\bin\wandaac.exe" }
if (-not (Test-Path $wandaac)) { Write-Error "compiler not found: $wandaac"; exit 1 }

$work = ".enc_verify"
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    Write-Host "==> generating the shared cases from the C++ encoder"
    g++ -std=c++17 -O2 -Wall -Wextra -o "$work\enc_script.exe" tools\enc\enc_script.cpp
    if ($LASTEXITCODE -ne 0) { Write-Error "could not build enc_script"; exit 1 }
    & "$work\enc_script.exe" "$work\script.txt" "$work\expect.hex"
    if ($LASTEXITCODE -ne 0) { Write-Error "enc_script failed"; exit 1 }

    Write-Host "==> building compiler/enc_check.waa with the C++ compiler"
    & $wandaac -q compiler\enc_check.waa "$work\enc_check.exe"
    if ($LASTEXITCODE -ne 0) { Write-Error "could not compile compiler/enc_check.waa"; exit 1 }

    Write-Host "==> encoding the same cases in Wandaa"
    & "$work\enc_check.exe" "$work\script.txt" 2>$null | Set-Content "$work\got.hex"

    $expect = Get-Content "$work\expect.hex"
    $got    = Get-Content "$work\got.hex"
    $script = Get-Content "$work\script.txt"

    if ($expect.Count -ne $got.Count) {
        Write-Error "WANDAA ENCODER MISMATCH: $($expect.Count) cases expected, $($got.Count) produced"
        exit 1
    }
    $bad = 0
    for ($i = 0; $i -lt $expect.Count; $i++) {
        if ($expect[$i] -cne $got[$i]) {
            if ($bad -lt 20) { Write-Host "$($script[$i]): expected $($expect[$i]), got $($got[$i])" }
            $bad++
        }
    }
    if ($bad -ne 0) { Write-Error "WANDAA ENCODER MISMATCH: $bad of $($expect.Count) cases differ"; exit 1 }

    Write-Host ""
    Write-Host "WANDAA ENCODER OK: $($expect.Count) cases encode identically in Wandaa and in C++"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
