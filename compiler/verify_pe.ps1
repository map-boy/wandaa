# ===========================================================================
#  Ground truth for the Wandaa PE writer, on Windows.
#
#  The PowerShell twin of compiler/verify_pe.sh, for the CI job that runs
#  native .exe files rather than wine. Builds the shared cases with both the
#  C++ PEWriter and compiler/pe.waa and requires identical images.
# ===========================================================================
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$wandaac = if ($env:WANDAAC) { $env:WANDAAC } else { ".\bin\wandaac.exe" }
if (-not (Test-Path $wandaac)) { Write-Error "compiler not found: $wandaac"; exit 1 }

$work = ".pe_verify"
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    Write-Host "==> building the cases with the C++ PEWriter"
    g++ -std=c++17 -O2 -Wall -Wextra -o "$work\pe_run.exe" tools\pe\pe_run.cpp
    if ($LASTEXITCODE -ne 0) { Write-Error "could not build pe_run"; exit 1 }
    & "$work\pe_run.exe" tools\pe\pe_cases.txt | Set-Content "$work\expect.hex"

    Write-Host "==> building compiler/pe_check.waa with the C++ compiler"
    & $wandaac -q compiler\pe_check.waa "$work\pe_check.exe"
    if ($LASTEXITCODE -ne 0) { Write-Error "could not compile compiler/pe_check.waa"; exit 1 }

    Write-Host "==> building the same cases in Wandaa"
    & "$work\pe_check.exe" tools\pe\pe_cases.txt 2>$null | Set-Content "$work\got.hex"

    $expect = Get-Content "$work\expect.hex"
    $got    = Get-Content "$work\got.hex"
    if ($expect.Count -ne $got.Count) {
        Write-Error "WANDAA PE WRITER MISMATCH: $($expect.Count) images expected, $($got.Count) produced"
        exit 1
    }
    $bad = 0
    for ($i = 0; $i -lt $expect.Count; $i++) {
        if ($expect[$i] -cne $got[$i]) {
            $bad++
            Write-Host "  case $($i+1) differs: $($expect[$i].Length) vs $($got[$i].Length) hex digits"
            for ($j = 0; $j -lt $expect[$i].Length -and $j -lt $got[$i].Length; $j += 2) {
                if ($expect[$i].Substring($j,2) -cne $got[$i].Substring($j,2)) {
                    Write-Host "    first difference at byte $($j/2)"
                    break
                }
            }
        }
    }
    if ($bad -ne 0) { Write-Error "WANDAA PE WRITER MISMATCH: $bad of $($expect.Count) images differ"; exit 1 }

    Write-Host ""
    Write-Host "WANDAA PE WRITER OK: $($expect.Count) images build identically in Wandaa and in C++"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
