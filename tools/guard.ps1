param(
    [switch]$Baseline,
    [string]$Wandaac = ".\bin\wandaac.exe"
)

$ErrorActionPreference = "Stop"
$guardDir = ".guard"
$baselinePath = Join-Path $guardDir "baseline.json"
New-Item -ItemType Directory -Path $guardDir -Force | Out-Null

function Build-Compiler {
    Write-Host "== Building compiler ==" -ForegroundColor Cyan
    if (-not (Test-Path bin)) { New-Item -ItemType Directory -Path bin | Out-Null }
    g++ -std=c++17 -O2 -Wall -Wextra -o bin\wandaac.exe src\lexer.cpp src\parser.cpp src\modules.cpp src\codegen.cpp src\main.cpp
    if ($LASTEXITCODE -ne 0) { throw "bin\wandaac.exe build failed" }
    g++ -std=c++17 -O2 -Wall -Wextra -o bin\wandaa.exe src\cli.cpp -lws2_32
    if ($LASTEXITCODE -ne 0) { throw "bin\wandaa.exe build failed" }
}

function Run-EndToEndTests {
    Write-Host "== Running tests\run_tests.ps1 ==" -ForegroundColor Cyan
    $output = & .\tests\run_tests.ps1 -Wandaac $Wandaac 2>&1 | Out-String
    $results = @{}
    foreach ($line in ($output -split "`n")) {
        if ($line -match "^\s*(PASS|FAIL)\s+(\S+)") {
            $results[$Matches[2]] = $Matches[1]
        }
    }
    return $results
}

function Run-EncoderVerify {
    Write-Host "== Running instruction-encoder ground truth (verify_enc) ==" -ForegroundColor Cyan
    Push-Location tools\enc
    try {
        g++ -std=c++17 -O2 -I..\..\include -o verify_enc.exe verify_enc.cpp
        if ($LASTEXITCODE -ne 0) { throw "verify_enc build failed" }
        .\verify_enc.exe --bin enc_out.bin
        if ($LASTEXITCODE -ne 0) { throw "verify_enc --bin failed to run" }
        $hash = (Get-FileHash enc_out.bin -Algorithm SHA256).Hash

        # If a bash toolchain (WSL/mingw) is available, run the full GNU-as diff too
        $bashDiffStatus = "SKIPPED (no bash/GNU-as toolchain found)"
        if (Get-Command bash -ErrorAction SilentlyContinue) {
            $prevEAP = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            & bash ./run_verify.sh *>&1 | Out-Null
            $exitCode = $LASTEXITCODE
            $ErrorActionPreference = $prevEAP
            $bashDiffStatus = if ($exitCode -eq 0) { "PASS" } else { "FAIL" }
        }
        return @{ EncoderHash = $hash; GnuAsDiff = $bashDiffStatus }
    } finally {
        Remove-Item enc_out.bin -ErrorAction SilentlyContinue
        Pop-Location
    }
}

# ---- main flow ----
Build-Compiler
$testResults = Run-EndToEndTests
$encResults  = Run-EncoderVerify

$snapshot = @{
    Timestamp    = (Get-Date).ToString("o")
    Tests        = $testResults
    Encoder      = $encResults
}

if ($Baseline) {
    $snapshot | ConvertTo-Json -Depth 5 | Set-Content -Path $baselinePath
    Write-Host "`nBaseline saved to $baselinePath" -ForegroundColor Green
    exit 0
}

if (-not (Test-Path $baselinePath)) {
    Write-Host "`nNo baseline found. Run '.\guard.ps1 -Baseline' first on known-good code." -ForegroundColor Yellow
    exit 1
}

$baselineData = Get-Content $baselinePath -Raw | ConvertFrom-Json

Write-Host "`n== Comparing against baseline ($($baselineData.Timestamp)) ==" -ForegroundColor Cyan

$regressions = @()

# Compare per-test results
foreach ($name in $testResults.Keys) {
    $old = $baselineData.Tests.$name
    $new = $testResults[$name]
    if ($old -eq "PASS" -and $new -eq "FAIL") {
        $regressions += "TEST REGRESSED: $name (was PASS, now FAIL)"
    }
}
# Catch tests that existed in baseline but vanished now (e.g. accidentally deleted case)
foreach ($name in $baselineData.Tests.PSObject.Properties.Name) {
    if (-not $testResults.ContainsKey($name)) {
        $regressions += "TEST MISSING: $name (was in baseline, not found now)"
    }
}

# Compare encoder ground truth
if ($baselineData.Encoder.EncoderHash -ne $encResults.EncoderHash) {
    $regressions += "ENCODER OUTPUT CHANGED: byte-for-byte hash differs from baseline (expected if intentional, otherwise investigate)"
}
if ($encResults.GnuAsDiff -eq "FAIL") {
    $regressions += "ENCODER GROUND-TRUTH FAIL: verify_enc output no longer matches GNU as"
}

if ($regressions.Count -gt 0) {
    Write-Host "`nREGRESSIONS FOUND:" -ForegroundColor Red
    $regressions | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host "`nDo not commit until these are resolved (or re-run -Baseline if the change is intentional).`n" -ForegroundColor Yellow
    exit 1
} else {
    Write-Host "`nNo regressions. Safe to commit.`n" -ForegroundColor Green
    exit 0
}



