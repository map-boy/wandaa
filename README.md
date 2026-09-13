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
· `hagarika`/`komeza` = break/continue · `injiza` = import · `hanze` = extern

Full keyword and operator tables: **[docs/ururimi.md](docs/ururimi.md)**.

---

## Gutangira — Getting started

```powershell
git clone https://github.com/map-boy/wandaa.git
cd wandaa
.\build.ps1 examples\mbere.waa
```

That builds the compiler and uses it to compile and run an example.
To compile a single file:

```powershell
.\wandaac.exe program.waa
.\program.exe
```

Or start a project, with dependencies and tests:

```powershell
.\wandaa.exe tangira umushinga-wanjye
cd umushinga-wanjye
..\wandaa.exe koresha
```

**Ibisabwa — requirements:** a C++17 compiler (`g++`) to build the compiler
once. After that, nothing. `wandaac.exe` writes the `.exe` itself — there is no
assembler and no linker in the pipeline, and the programs it produces run on
any Windows x86-64 machine with nothing installed.

---

## Icyo ishoboye — What you can write today

Everything below is implemented and covered by the test suite. It is not a
preview: if a keyword appears here, it works.

**Imibare n'amagambo — numbers and text.** 64-bit integers, and strings with an
O(1) length, concatenation, comparison and slicing.

```wandaa
reka izina = "Mugisha";
reka ubutumwa = "Mwiriwe " + izina;
andika(ubutumwa);
andika(uburebure(ubutumwa));
```

**Imirimo — functions**, with recursion and no argument-count limit.

```wandaa
umurimo fibonacci(n) {
  niba (n < 2) { tanga n; }
  tanga fibonacci(n - 1) + fibonacci(n - 2);
}
andika(fibonacci(20));
```

**Intonde — arrays**, literal or sized at runtime, on the heap.

```wandaa
reka a = [10, 20, 30];
a[1] = 99;
reka b = urutonde(100);        # 100 elements, all 0
```

**Injiza — modules**, and a standard library written in Wandaa itself.

```wandaa
injiza "imibare.waa";
andika(mugabane(48, 18));      # 6
```

**Hanze — FFI.** Call any function in any Windows DLL, with no glue code and no
compiler support for the specific library. This is the mechanism the database,
networking and ML work in the roadmap is built on.

```wandaa
hanze "user32.dll" MessageBoxA(hwnd, ubutumwa, umutwe, ubwoko);
MessageBoxA(0, "Mwiriwe", "Wandaa", 0);
```

**Amakosa — errors.** A runtime fault reports the source line rather than a raw
exit code:

```
Ikosa ku murongo: 3
```

For what is *not* here yet, see **[ROADMAP.md](ROADMAP.md)** — it is the single
source of truth for feature status, and this page deliberately does not
duplicate it.

---

## Inyandiko — Documentation

| | |
|---|---|
| [docs/kwiga.md](docs/kwiga.md) | **Kwiga Wandaa** — tutorial from zero, no prior programming assumed |
| [docs/ururimi.md](docs/ururimi.md) | Language reference — keywords, operators, semantics |
| [docs/isomero.md](docs/isomero.md) | Standard library reference (generated from `lib/`) |
| [docs/ibikoresho.md](docs/ibikoresho.md) | The `wandaa` project and package tool |
| [docs/imbere.md](docs/imbere.md) | Compiler internals — how the `.exe` gets written |
| [docs/UPDATING.md](docs/UPDATING.md) | How to change the compiler, encoder, runtime or stdlib |
| [ROADMAP.md](ROADMAP.md) | **Feature status** — what is built, what is not, in what order |

---

## Aho igeze — Status

Wandaa is **pre-1.0**.

**[ROADMAP.md](ROADMAP.md) is the single source of truth for what is
implemented.** Per-feature status is tracked there and nowhere else, so this
section stays short on purpose — it will not quietly disagree with the
roadmap as phases land.

The limitations worth knowing before you start, as the roadmap states them:

- **No floating point.** Blocks statistics, graphics, money-with-cents, and ML.
- **No memory reclamation.** Long-running processes that build strings in a
  loop grow until they exit. Fine for tools and batch jobs; not yet for daemons.
- **No bounds checking** on array indexing.
- **Windows x86-64 only.**

What is solid is solid because it is **checked, not asserted**:

- Every machine-code encoding the compiler emits is diffed byte-for-byte
  against GNU `as` in CI. The harness emits the same instruction sequence twice
  — once as assembler text, once through the encoder — assembles the text with
  the real assembler, and requires every byte to match.
- The runtime blob is regenerated from its assembly source on every push and
  the build fails if the committed bytes differ.
- CI compiles a program on Windows with `PATH` stripped to the system
  directories, so `gcc`, `as` and `ld` are unreachable, then runs the result.
  That is the regression test for the "no external toolchain" claim itself.
- The generated executables are run on two platforms — natively on Windows and
  under Wine on Linux — and their output compared against frozen expectations.

[docs/UPDATING.md](docs/UPDATING.md) explains how to run all of that locally.

---

## Gufasha — Contributing

Umusanzu wawe wakirwa. Contributions are welcome, in **Kinyarwanda, English or
French**.

You do not need to know x86-64. Much of the highest-value work — the standard
library, DLL bindings, examples, documentation — is written in Wandaa itself;
those issues carry the `written-in-wandaa` label.

| | |
|---|---|
| [CONTRIBUTING.md](CONTRIBUTING.md) | Where to start, and what to run before opening a PR |
| [docs/UPDATING.md](docs/UPDATING.md) | How a compiler, encoder, runtime or stdlib change is actually made and verified |
| [GOVERNANCE.md](GOVERNANCE.md) | How decisions get made, and the design-proposal process for language changes |
| [ROADMAP.md](ROADMAP.md) | What needs doing, and what it is blocked on |

---

## Uburenganzira — Licence and stewardship

MIT — see [LICENSE](LICENSE).

Wandaa is stewarded by **VAF UBWENGE TECH**, which provides commercial support,
training and long-term support builds. See [ENTERPRISE.md](ENTERPRISE.md). The
compiler is and stays MIT-licensed; there is no open-core split.
