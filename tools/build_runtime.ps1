# Regenerate include\runtime_blob.hpp from tools\runtime\runtime.s.
#
# BUILD-TIME ONLY -- the one place an assembler is used, on the Wandaa
# compiler's own build machine. End users only ever run wandaac.exe.
#
# Requires MinGW-w64 (as.exe / g++.exe) on PATH.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("wandaa_rt_" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $as = if (Get-Command x86_64-w64-mingw32-as -ErrorAction SilentlyContinue) { "x86_64-w64-mingw32-as" } else { "as" }

    Write-Host "==> assembling tools\runtime\runtime.s"
    & $as -o "$tmp\runtime.o" tools\runtime\runtime.s
    if ($LASTEXITCODE -ne 0) { throw "assembler failed" }

    Write-Host "==> building extractor"
    g++ -std=c++17 -O2 -Wall -Wextra -o "$tmp\extract_blob.exe" tools\extract_blob.cpp
    if ($LASTEXITCODE -ne 0) { throw "extractor build failed" }

    Write-Host "==> extracting blob"
    & "$tmp\extract_blob.exe" "$tmp\runtime.o" include\runtime_blob.hpp
    if ($LASTEXITCODE -ne 0) { throw "extraction failed" }

    Write-Host "==> ok: include\runtime_blob.hpp regenerated"
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
