param([string]$Source = "examples\mbere.waa")
g++ -std=c++17 -O2 -o wandaac.exe src\lexer.cpp src\parser.cpp src\codegen.cpp src\main.cpp
if ($LASTEXITCODE -ne 0) { exit 1 }
.\wandaac.exe $Source
if ($LASTEXITCODE -ne 0) { exit 1 }
$exe = [System.IO.Path]::ChangeExtension($Source, ".exe")
gcc -nostdlib -nostartfiles "-Wl,--entry=_start" "$Source.s" -o $exe -lkernel32
if ($LASTEXITCODE -ne 0) { exit 1 }
Write-Host "Byubatswe: $exe"
& ".\$exe"
