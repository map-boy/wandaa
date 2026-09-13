# Gutanga umusanzu muri Wandaa — Contributing to Wandaa

Wandaa ni ururimi rwa mudasobwa rukoresha amagambo y'Ikinyarwanda, ruhindura
porogaramu mu byuma bya Windows x86-64. Umusanzu wawe wakirwa neza.

Wandaa is a compiled language with Kinyarwanda keywords targeting native
Windows x86-64. Contributions are welcome.

**Ururimi — Language.** Issues, pull requests and reviews are welcome in
**Kinyarwanda, English or French**. You are not required to switch languages to
be taken seriously. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

---

## Gutangira — Getting started

```powershell
git clone https://github.com/map-boy/wandaa.git
cd wandaa
.\build.ps1 examples\mbere.waa
```

Kuri Linux (ukoresha Wine kugira ngo ugerageze) — on Linux, using Wine to run
the output:

```bash
./build.sh
./tests/run_tests.sh
```

Ibisabwa — requirements: a C++17 compiler (`g++`) to build `wandaac`. That is
all. Building Wandaa programs needs nothing else — no assembler, no linker.

Only two things need extra tools, and only for maintainers:

- Changing `tools/runtime/runtime.s` needs `x86_64-w64-mingw32-as`
- Running the encoder ground-truth test needs the same

---

## Uko umushinga wubatse — Project layout

```
src/          compiler: lexer, parser, module resolver, code generator
include/      pe_writer.hpp, x64asm.hpp, runtime_blob.hpp (generated)
lib/          the standard library, written in Wandaa
tools/        build-time tooling: runtime assembly, blob extractor, encoder test
tests/        test cases, frozen expected output, and the runners
docs/         language reference, tutorial, stdlib reference, internals
examples/     sample programs
```

Read [docs/imbere.md](docs/imbere.md) before touching the code generator. It
explains the section layout, the runtime blob, and why the design is what it is.

---

## Uburyo bwo gufasha — Ways to contribute

**Ntabwo ari ngombwa kumenya x86-64.** You do not need to know assembly. The
highest-value work right now mostly does not touch the compiler at all:

| Aho wafasha | Icyo bisaba |
|---|---|
| **Isomero rusange** (`lib/*.waa`) | Kwandika Wandaa gusa — pure Wandaa |
| **FFI bindings** (SQLite, sockets, ONNX) | Wandaa + reading a C API reference |
| **Inyandiko / guhindura** | Kinyarwanda, English or French |
| **Ingero** (`examples/`) | Wandaa |
| **Ibikoresho** (CLI, formatter, LSP) | C++ or any language |
| **Compiler internals** | C++17, and for codegen, x86-64 |

Issues labelled `written-in-wandaa` need no compiler knowledge at all.
Issues labelled `good first issue` are scoped small on purpose.

See [ROADMAP.md](ROADMAP.md) for what is planned and in what order, and
[MAINTAINERS.md](MAINTAINERS.md) for areas looking for an owner.

---

## Mbere yo gutanga PR — Before opening a pull request

**1. Gerageza byose — run the tests.**

```bash
./tests/run_tests.sh          # or .\tests\run_tests.ps1 on Windows
```

**2. Ongeraho igerageza — add a test.** Anything that changes behaviour needs a
case in `tests/cases/` and its expected output in `tests/expected/`:

```bash
./wandaac tests/cases/ikintu.waa /tmp/ikintu.exe
wine /tmp/ikintu.exe > tests/expected/ikintu.out
echo $? > tests/expected/ikintu.exit
```

**3. Niba wahinduye `include/x64asm.hpp` cyangwa codegen** — if you touched the
encoder or code generator:

```bash
./tools/enc/run_verify.sh
```

Every byte must match GNU `as`. This is not optional and CI enforces it. If you
add an instruction form, add it to `tools/enc/verify_enc.cpp` too — an
unverified encoding is the single easiest way to introduce a bug that only
shows up on someone else's machine.

**4. Niba wahinduye `tools/runtime/runtime.s`:**

```bash
./tools/build_runtime.sh      # regenerates include/runtime_blob.hpp
```

Commit the regenerated header. CI regenerates it and fails if it differs.

**5. Niba wahinduye `lib/`:**

```bash
python3 tools/gen_stdlib_docs.py
```

Commit the regenerated `docs/isomero.md`.

---

## Impinduka ku ururimi — Language changes

Keywords, syntax, data representation, the FFI, module resolution, and anything
that breaks existing programs need a **Wandaa Design Proposal** first — an
issue using the [design proposal template](.github/ISSUE_TEMPLATE/design_proposal.md).
It stays open at least seven days. See [GOVERNANCE.md](GOVERNANCE.md).

Opening a WDP before writing the code is not bureaucracy; it stops you spending
a weekend on something that will be declined on naming grounds.

**Amazina — naming.** New keywords and library functions use Kinyarwanda words
a speaker would actually use for the concept, not transliterated English. If
you are unsure of the right word, say so in the proposal and ask — getting this
right is a contribution in itself.

---

## Uko PR isuzumwa — How pull requests are reviewed

- Keep a pull request to one change. Two unrelated fixes are two PRs.
- Explain **why**, not what. The diff says what.
- All CI jobs must be green: the Windows build and tests, the bare-PATH
  self-containment check, the encoder ground-truth diff, the runtime blob
  freshness check, and the Linux/Wine run.
- One committer approval merges. Language changes need an accepted WDP first.

Review comments are about the code. If a review feels personal, say so — see
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

---

## Guhinduka ku mwanya — Becoming a committer

Roughly five merged non-trivial pull requests, plus review comments that show
you understand why the code is shaped the way it is. Maintainers make the call;
see [GOVERNANCE.md](GOVERNANCE.md). Ask if you want to know where you stand —
we would rather tell you than have you guess.

---

## Umutekano — Security

Do not open a public issue for a security problem. See [SECURITY.md](SECURITY.md).
