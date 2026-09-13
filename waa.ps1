# waa <file>.waa  -- compile-and-run a single Wandaa file, like `python file.py`.
# Caches the built .exe in a .waa_cache folder next to the source and only
# recompiles when the .waa file is newer than the cached .exe.
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
    & $compiler $srcFull $exePath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

& $exePath @RunArgs
exit $LASTEXITCODE