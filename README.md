# Wandaa

A small compiled programming language with Kinyarwanda keywords, compiling straight to native x86-64 Windows executables - no VM, no interpreter.

```
reka x = 10;
reka y = 20;
andika(x + y * 2);
```

`reka` = let, `andika` = print, `niba`/`ubundi` = if/else, `mugihe` = while, `umurimo` = function, `tanga` = return.

## Status

Windows only, for now. Requires MinGW-w64 (g++/gcc) on your PATH.

Crashes (div-by-zero, bad memory access, etc.) are caught at runtime and reported with the source line number instead of a raw exit code.

## Build and run

```powershell
git clone https://github.com/map-boy/wandaa.git
cd wandaa
.\waa.bat examples\gito.waa
```
