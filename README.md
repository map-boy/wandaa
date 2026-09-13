# Wandaa

**Ururimi rwa mudasobwa rukoresha amagambo y'Ikinyarwanda.**
A compiled programming language with Kinyarwanda keywords, compiling straight
to native x86-64 Windows executables — no VM, no interpreter, no external
toolchain.

```wandaa
reka x = 10;
reka y = 20;
andika(x + y * 2);
```

```
50
```

`reka` = let · `andika` = print · `niba`/`ubundi` = if/else · `mugihe` = while
· `umurimo` = function · `tanga` = return · `na`/`cyangwa`/`si` = and/or/not

---

## Gutangira — Getting started

```powershell
git clone https://github.com/map-boy/wandaa.git
cd wandaa
.\build.ps1 examples\mbere.waa
```

That builds `wandaac.exe` and uses it to compile and run an example.
To compile your own program:

```powershell
.\wandaac.exe program.waa
.\program.exe
```

**Ibisabwa — requirements:** a C++17 compiler (`g++`) to build the compiler
once. After that, nothing. `wandaac.exe` writes the `.exe` itself — there is no
assembler and no linker in the pipeline, and the programs it produces run on
any Windows x86-64 machine with nothing installed.

---

## Icyo ishoboye — What it can do

- Imibare, amagambo, intonde — integers, strings, arrays
- Imirimo n'ubwisubire — functions and recursion, any number of arguments
- `niba` / `mugihe` / `hagarika` / `komeza` — control flow
- **`hanze`** — call any function in any Windows DLL, with no glue code
- **`injiza`** — modules, and a standard library written in Wandaa
- Amakosa avuga umurongo — runtime faults report the source line

```wandaa
injiza "amagambo.waa";
hanze "user32.dll" MessageBoxA(hwnd, ubutumwa, umutwe, ubwoko);

MessageBoxA(0, hejuru("mwiriwe"), "Wandaa", 0);
```

---

## Inyandiko — Documentation

| | |
|---|---|
| [docs/kwiga.md](docs/kwiga.md) | **Kwiga Wandaa** — tutorial from zero |
| [docs/ururimi.md](docs/ururimi.md) | Language reference |
| [docs/isomero.md](docs/isomero.md) | Standard library reference |
| [docs/imbere.md](docs/imbere.md) | Compiler internals |
| [ROADMAP.md](ROADMAP.md) | What is built, what is not, and in what order |

---

## Aho igeze — Status

Wandaa is **pre-1.0**, and [ROADMAP.md](ROADMAP.md) is explicit about what is
missing. The most significant gaps: **no floating point**, **no memory
reclamation**, and **no bounds checking on array indexing**. Windows only.

What is solid: the compiler produces correct native executables with no
external toolchain, and it is verified rather than assumed —
7,500+ instruction encodings are diffed byte-for-byte against GNU `as` in CI,
the runtime blob is regenerated and checked for drift on every push, and CI
compiles a program on Windows with `PATH` stripped so `gcc`, `as` and `ld` are
unreachable, then runs the result.

---

## Gufasha — Contributing

Umusanzu wawe wakirwa. Contributions are welcome, in **Kinyarwanda, English or
French**.

You do not need to know x86-64 — much of the highest-value work
(standard library, DLL bindings, examples, documentation) is written in Wandaa
itself. See [CONTRIBUTING.md](CONTRIBUTING.md), [GOVERNANCE.md](GOVERNANCE.md)
and the `good first issue` label.

---

## Uburenganzira — Licence and stewardship

MIT — see [LICENSE](LICENSE).

Wandaa is stewarded by **VAF UBWENGE TECH**, which provides commercial support,
training and long-term support builds. See [ENTERPRISE.md](ENTERPRISE.md). The
compiler is and stays MIT-licensed; there is no open-core split.
