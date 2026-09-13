# waa <file>.waa  -- compile-and-run a single Wandaa file, like `python file.py`.
#
# The built .exe goes in a .waa_cache folder beside the source (the equivalent
# of __pycache__) and is reused until the source is newer, so nothing clutters
# the directory you are working in. Compile errors are printed under a clear
# header so they are never confused with the program's own output.
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$RunArgs
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Source)) {
    Write-Host "Ntibonetse (not found): $Source" -ForegroundColor Red
    exit 1
}

$srcFull   = (Resolve-Path $Source).Path
$srcDir    = Split-Path $srcFull -Parent
$srcName   = [System.IO.Path]::GetFileNameWithoutExtension($srcFull)
$cacheDir  = Join-Path $srcDir ".waa_cache"
$exePath   = Join-Path $cacheDir "$srcName.exe"

if (-not (Test-Path $cacheDir)) { New-Item -ItemType Directory -Path $cacheDir | Out-Null }

# Locate wandaac.exe: same folder as this script, else WANDAAC env var, else PATH.
$compiler = $null
$besideScript = Join-Path $PSScriptRoot "wandaac.exe"
if (Test-Path $besideScript) { $compiler = $besideScript }
elseif ($env:WANDAAC -and (Test-Path $env:WANDAAC)) { $compiler = $env:WANDAAC }
else {
    $onPath = Get-Command wandaac.exe -ErrorAction SilentlyContinue
    if ($onPath) { $compiler = $onPath.Source }
}
if (-not $compiler) {
    Write-Host "wandaac.exe ntibonetse (compiler not found). Set `$env:WANDAAC or place it beside waa.ps1." -ForegroundColor Red
    exit 1
}

$needsBuild = $true
if (Test-Path $exePath) {
    $srcTime = (Get-Item $srcFull).LastWriteTimeUtc
    $exeTime = (Get-Item $exePath).LastWriteTimeUtc
    if ($exeTime -ge $srcTime) { $needsBuild = $false }
}

if ($needsBuild) {
    # -q: say nothing on success, so `waa x.waa` prints only what x.waa prints.
    $errors = & $compiler -q $srcFull $exePath 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "--- Ikosa ryo gukusanya (compile error) ---" -ForegroundColor Red
        $errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
        exit $LASTEXITCODE
    }
    $errors | ForEach-Object { Write-Host $_ }
}

& $exePath @RunArgs
exit $LASTEXITCODE