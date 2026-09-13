# Build the Wandaa compiler, then compile and run a Wandaa program with it.
#
# Building wandaac.exe needs a C++17 compiler. Running the .exe it produces
# needs nothing at all -- no assembler, no linker, no MinGW runtime.
param([string]$Source = "examples\mbere.waa")

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

g++ -std=c++17 -O2 -o wandaac.exe src\lexer.cpp src\parser.cpp src\codegen.cpp src\main.cpp
if ($LASTEXITCODE -ne 0) { exit 1 }

$exe = [System.IO.Path]::ChangeExtension($Source, ".exe")
.\wandaac.exe $Source $exe
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "Byubatswe: $exe"
& $exe
