# Wandaa end-to-end test suite (Windows).
#
# Compiles every example and test case with wandaac.exe -- no assembler, no
# linker -- runs the resulting native .exe, and asserts stdout and exit code
# match the frozen expectations in tests\expected\.
param([string]$Wandaac = ".\wandaac.exe")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not (Test-Path $Wandaac)) { Write-Error "compiler not found: $Wandaac"; exit 1 }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("wandaa_tests_" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $work | Out-Null

$pass = 0; $fail = 0
$sources = @(Get-ChildItem tests\cases\*.waa) + @(Get-ChildItem examples\*.waa)

foreach ($src in $sources) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($src.Name)
    $exe  = Join-Path $work "$name.exe"

    & $Wandaac $src.FullName $exe | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "FAIL  $name (compile)" -ForegroundColor Red; $fail++; continue }

    # Run from the work directory: some cases write a file next to themselves.
    Push-Location $work
    $actual   = & $exe 2>$null | Out-String
    $exitCode = $LASTEXITCODE
    Pop-Location

    $expectedOut  = (Get-Content "tests\expected\$name.out" -Raw -ErrorAction SilentlyContinue)
    $expectedExit = [int](Get-Content "tests\expected\$name.exit" -Raw).Trim()
    if ($null -eq $expectedOut) { $expectedOut = "" }

    # Normalise line endings so the expectations stay platform-neutral.
    $a = ($actual      -replace "`r`n", "`n")
    $e = ($expectedOut -replace "`r`n", "`n")

    if ($a -eq $e -and $exitCode -eq $expectedExit) {
        Write-Host "PASS  $name" -ForegroundColor Green; $pass++
    } else {
        Write-Host "FAIL  $name (exit $exitCode, expected $expectedExit)" -ForegroundColor Red
        Write-Host "  --- expected ---"; Write-Host $e
        Write-Host "  --- actual   ---"; Write-Host $a
        $fail++
    }
}

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "$pass passed, $fail failed"
if ($fail -gt 0) { exit 1 }
