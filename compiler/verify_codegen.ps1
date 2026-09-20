# ===========================================================================
#  Ground truth for the Wandaa code generator, on Windows.
#
#  The PowerShell twin of compiler/verify_codegen.sh. Every program in
#  tests/bootstrap/ must compile to a BYTE-IDENTICAL executable under both
#  the C++ compiler and compiler/codegen.waa.
# ===========================================================================
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$wandaac = if ($env:WANDAAC) { $env:WANDAAC } else { ".\bin\wandaac.exe" }
if (-not (Test-Path $wandaac)) { Write-Error "compiler not found: $wandaac"; exit 1 }

$work = ".codegen_verify"
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    Write-Host "==> building compiler/codegen.waa with the C++ compiler"
    & $wandaac -q compiler\codegen.waa "$work\codegen.exe"
    if ($LASTEXITCODE -ne 0) { Write-Error "could not compile compiler/codegen.waa"; exit 1 }

    $ok = 0
    $bad = 0
    foreach ($f in (Get-ChildItem tests\bootstrap -Filter *.waa | Sort-Object Name)) {
        $rel = Resolve-Path -Relative $f.FullName
        Remove-Item -Force "$work\cpp.exe", "$work\waa.exe" -ErrorAction SilentlyContinue
        & $wandaac -q $rel "$work\cpp.exe"
        if ($LASTEXITCODE -ne 0) { $bad++; Write-Host "cpp refused: $rel"; continue }

        & "$work\codegen.exe" $rel "$work\waa.exe" 2>&1 | Out-Null
        if (-not (Test-Path "$work\waa.exe")) { $bad++; Write-Host "FAILED TO BUILD: $rel"; continue }

        $a = Get-FileHash "$work\cpp.exe" -Algorithm SHA256
        $b = Get-FileHash "$work\waa.exe" -Algorithm SHA256
        if ($a.Hash -eq $b.Hash) { $ok++ } else { $bad++; Write-Host "DIFFERS: $rel" }
    }

    Write-Host ""
    if ($bad -ne 0) { Write-Error "WANDAA CODEGEN MISMATCH: $bad of $($ok+$bad) programs differ"; exit 1 }
    Write-Host "WANDAA CODEGEN OK: $ok programs compile to byte-identical executables"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
