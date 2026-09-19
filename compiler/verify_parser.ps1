# ===========================================================================
#  Ground truth for the Wandaa parser, on Windows.
#
#  The PowerShell twin of compiler/verify_parser.sh, for the CI job that runs
#  native .exe files rather than wine. Compiles compiler/parser.waa with the
#  C++ compiler, then requires both parsers to produce identical AST dumps
#  for every .waa file in the repository.
# ===========================================================================
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$wandaac = if ($env:WANDAAC) { $env:WANDAAC } else { ".\bin\wandaac.exe" }
if (-not (Test-Path $wandaac)) { Write-Error "compiler not found: $wandaac"; exit 1 }

$work = ".parser_verify"
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    Write-Host "==> building compiler/parser.waa with the C++ compiler"
    & $wandaac -q compiler\parser.waa "$work\parser.exe"
    if ($LASTEXITCODE -ne 0) { Write-Error "could not compile compiler/parser.waa"; exit 1 }

    $ok = 0
    $bad = 0
    $files = Get-ChildItem -Recurse -Filter *.waa -Path tests\cases, examples, lib, compiler |
             Where-Object { $_.Length -gt 0 } | Sort-Object FullName
    foreach ($f in $files) {
        $rel = Resolve-Path -Relative $f.FullName
        $cpp = & $wandaac --ast $rel 2>$null
        # A file the C++ lexer itself rejects has no stream to compare against.
        if ($LASTEXITCODE -ne 0) { continue }
        $waa = & "$work\parser.exe" $rel 2>$null

        $a = ($cpp | Out-String) -replace "`r`n", "`n"
        $b = ($waa | Out-String) -replace "`r`n", "`n"
        if ($a -ceq $b) {
            $ok++
        } else {
            $bad++
            Write-Host "DIFFERS: $rel"
            Compare-Object ($cpp) ($waa) | Select-Object -First 10 | Format-Table | Out-String | Write-Host
        }
    }

    Write-Host ""
    if ($bad -ne 0) { Write-Error "PARSER MISMATCH: $bad of $($ok + $bad) files differ"; exit 1 }
    Write-Host "PARSER OK: $ok files parse to an identical tree in Wandaa and in C++"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
