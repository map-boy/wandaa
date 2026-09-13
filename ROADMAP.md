# Wandaa Roadmap

What exists, what does not, and the order things have to happen in.

Status labels: **Done** · **In progress** · **Designed** (approach settled, not
built) · **Planned** (agreed it should exist) · **Research** (not yet solved).

---

## Where the project actually is

**Done:**

- Lexer, parser, AST, type inference for int / string / array
- Native x86-64 Windows PE output with **no assembler and no linker** —
  `wandaac.exe` is the only tool an end user runs
- Verified instruction encoder (7,500+ instructions diffed byte-for-byte
  against GNU `as` in CI)
- Runtime blob pipeline: fixed runtime assembled once at compiler build time,
  extracted with enforced position-independence, freshness-checked in CI
- Functions, recursion, any number of arguments, correct Win64 ABI including
  stack alignment
- Strings with O(1) length, concatenation, comparison, slicing, ASCII case
- Heap arrays, literal and dynamically sized
- Short-circuit `na`/`cyangwa`/`si`, `hagarika`/`komeza`
- **FFI** — `hanze` calls any function in any Windows DLL
- **Modules** — `injiza`, with include-once and cycle detection
- A standard library written in Wandaa (`lib/`)
- Runtime fault reporting with source line numbers
- The `wandaa` project tool: scaffolding, builds, tests, and dependency
  vendoring with a SHA-256 lockfile that is actually verified
- **f64 floating point**, with integer division left unchanged
- 21 end-to-end tests, differential-tested against the previous backend

**The limitations that block the most:**

1. **No memory reclamation.** Blocks long-running servers.
2. **No records/structs.** Blocks ergonomic libraries and self-hosting.

Floating point was the third and is now **Done** — see Phase 1. Everything
remaining in Phase 1 below exists to remove the other two.

---

## Phase 1 — Language completeness

The prerequisite for everything else. Nothing downstream is worth starting
before this lands.

| Item | Status | Notes |
|---|---|---|
| **Floating point (`f64`)** | **Done** | SSE2 in the encoder (all forms diffed against GNU `as` across XMM0-15), `VType::Float`, float literals, XMM0-3 in the calling convention, and 6-decimal trimmed printing in the runtime. Integer division is unchanged: `7 / 2` is still 3, and promotion happens only when an operand is already a float. NaN compares false against everything including itself. See WDP #2. Not yet: reading a `double` RETURNED by a `hanze` function, which Win64 passes back in XMM0 rather than RAX. |
| **Records (`ubwoko`)** | Designed | Named fields over a heap block, laid out like arrays with a type tag. Field access is a constant offset — no dictionary lookup. |
| **`for` loops** | Planned | `kuri i muri 0..n` — desugars to the existing `mugihe`. |
| **Bounds-checked indexing** | Planned | Currently unchecked. The check reuses the array count header and the existing crash handler, so it reports a source line. Opt-out for hot loops. |
| **Memory reclamation** | Research | Reference counting is the likely answer: predictable, no pause, and implementable without a shadow stack. Cycles leak, which for this language is an acceptable first step. Needs settling before any server work. |
| **Explicit types (optional)** | Planned | Inference stays the default; annotations become possible for FFI signatures and public APIs. |
| **Multi-file line numbers** | Planned | `wandaa_current_line` is a single slot; with modules a fault should name the file too. |
| **UTF-8 aware strings** | Planned | `inyuguti()`/`igice()` work on bytes. Kinyarwanda is ASCII-representable, but the strings a program *handles* are not necessarily. |

---

## Phase 2 — Tooling

| Item | Status | Notes |
|---|---|---|
| **`wandaa` CLI** | **Done** | `tangira` (new), `ongeraho` (add a dependency), `shakisha` (fetch), `genzura` (verify), `yubaka` (build), `koresha` (run), `gerageza` (test), `verisiyo`. `wandaac` stays as the bare compiler underneath. `hindura` (fmt) is not implemented — see Formatter. |
| **Package manager — core** | **Done** | `wandaa.toml` manifest, local-path and git dependencies, vendoring into `ibipapuro/`, `wandaa.lock` with a SHA-256 over each package, and `genzura` to verify it. Builds are offline once `ibipapuro/` exists. |
| **Package manager — registry** | Planned | Today a dependency is a local path or a git URL. A named, versioned registry with semver resolution does not exist yet. See below. |
| **Test runner** | **Done** (file-based) | `wandaa gerageza` compiles and runs every `.waa` in `tests/`; a test passes when it exits 0. In-source `gerageza` blocks are still Planned. |
| **Formatter** | Planned | Reuses the existing parser; prints the AST. One canonical style, no options. Would become `wandaa hindura`. |
| **Language server (LSP)** | Planned | Completion, go-to-definition, inline errors. The parser already tracks line numbers. |
| **Debugger support** | Research | Requires emitting PDB or DWARF, and splitting `.text` from `.data` first. |

### Package manager: what ships today, and what does not

**Today.** A manifest, `wandaa.toml`:

```toml
[umushinga]              # project
izina = "urubuga-rwanjye"
verisiyo = "0.1.0"
intangiriro = "src/mbere.waa"

[ibisabwa]               # dependencies
ibipimo = { inzira = "../ibipimo" }                          # local path
json    = { git = "https://github.com/...", tag = "v0.3" }   # git
```

`wandaa shakisha` vendors each dependency into `ibipapuro/`, which is passed to
the compiler as `-I` search paths, and writes `wandaa.lock` recording a SHA-256
over each package's sorted `.waa` paths and contents. `wandaa genzura`
re-hashes and fails on drift. Once `ibipapuro/` exists — fetched once, or
committed — **building needs no network at all**, and `git` is needed only to
fetch a git dependency in the first place.

That offline property is a requirement, not a side effect: a package manager
that assumes reliable broadband is not usable everywhere this language needs to
work.

**Not yet.** There is no registry, so `json = "1.2"` is not a thing you can
write — a dependency is a path or a git URL. Semver resolution, a mirrorable
static index, and package signing are all still Planned.

---

## Phase 3 — Data and databases

Nothing here needs compiler changes. FFI is the whole mechanism.

| Item | Status | Notes |
|---|---|---|
| **SQLite driver** | Designed | `hanze "sqlite3.dll"` over `sqlite3_open`, `_prepare_v2`, `_step`, `_column_*`, `_finalize`. Needs records (Phase 1) for row values. |
| **PostgreSQL driver** | Planned | Same approach via `libpq.dll`. |
| **ODBC** | Planned | Reaches SQL Server and anything else with a driver. |
| **Connection pooling** | Planned | Needs memory reclamation. |
| **ORM (`ubutunzi`)** | Designed | Records map to tables; a query builder produces parameterised SQL. Parameterised from day one — a language taught to beginners should make SQL injection hard to write by accident. Migrations generated from record definitions. |
| **CSV / JSON** | Planned | Pure Wandaa, in `lib/`. JSON needs records. |

---

## Phase 4 — Web and servers

| Item | Status | Notes |
|---|---|---|
| **Sockets** | Designed | `hanze "ws2_32.dll"` over `WSAStartup`, `socket`, `bind`, `listen`, `accept`, `recv`, `send`. |
| **HTTP/1.1 parser** | Planned | Pure Wandaa. |
| **Server framework (`urubuga`)** | Designed | Routing, request/response records, middleware. Needs records and memory reclamation — a server that leaks per request is not a server. |
| **HTTP client** | Planned | Via WinHTTP, which handles TLS for us rather than us shipping a TLS stack. |
| **Templating** | Planned | For HTML rendering. |

---

## Phase 5 — AI and machine learning

Floating point landed in Phase 1, so the hard prerequisite is cleared. What
remains below is designed but not built; tensors and the ONNX Runtime binding
are now unblocked and are the next things anyone can pick up.

| Item | Status | Notes |
|---|---|---|
| **`f64` arithmetic** | **Done** (Phase 1) | Was the hard prerequisite for everything else here. |
| **Tensors (`ikibumbano`)** | Designed | N-dimensional f64 array with shape metadata; strided views. |
| **BLAS via FFI** | Designed | Bind OpenBLAS/MKL rather than writing matrix multiply. The FFI already supports this; a real GEMM is not something to reimplement. |
| **ONNX Runtime binding** | Designed | `onnxruntime.dll` gives inference for models trained elsewhere. **This is the highest-value first step**: it makes Wandaa useful for deploying models immediately, without Wandaa having to become a training framework. |
| **Automatic differentiation** | Research | Tape-based reverse mode. Only worth it after tensors are solid. |
| **Classical ML in `lib/`** | Planned | Linear/logistic regression, k-means, decision trees, written in Wandaa as readable teaching material. |
| **Data frames** | Planned | Needs records and strings-in-columns. |

The strategic point: **Wandaa does not need to reimplement PyTorch.** It needs
to call the runtimes that already exist, and to make the surrounding
program — data loading, preprocessing, serving — writable in Kinyarwanda.

---

## Phase 6 — Self-hosting

The goal: **Wandaa's compiler written in Wandaa**, compiling itself.

This is not vanity. A self-hosted compiler is the strongest possible evidence
that the language is complete enough for serious work, and it makes every
compiler contributor a Wandaa programmer.

**What it requires** (all Phase 1 work, which is why it comes last):

- Records — an AST is a tree of tagged nodes
- Growable arrays and a map type — symbol tables
- String building that does not leak — the code generator produces a lot of it
- File I/O — already present
- Enough recursion depth for deeply nested expressions

**The bootstrap sequence:**

| Stage | What | Status |
|---|---|---|
| 0 | The current C++ compiler | **Done** |
| 1 | Phase 1 language features, so a compiler is writable | Planned |
| 2 | `compiler/*.waa` — lexer, parser, codegen in Wandaa | Planned |
| 3 | Compile stage 2 with stage 0 → `wandaac-s1.exe` | Planned |
| 4 | Compile stage 2 with `wandaac-s1.exe` → `wandaac-s2.exe` | Planned |
| 5 | **Assert `wandaac-s1.exe` and `wandaac-s2.exe` are byte-identical** | Planned |

Stage 5 is the classic bootstrap fixpoint proof: a compiler that compiles
itself to the same bytes twice is consistent. It becomes a CI job.

The C++ compiler is deliberately written in a simple, transliterable style —
no template metaprogramming, no exceptions used for control flow, explicit data
structures — so stage 2 is a translation rather than a redesign.

---

## Phase 7 — Beyond Windows

| Item | Status | Notes |
|---|---|---|
| **ELF output (Linux)** | Planned | The code generator is already split from the container format. Needs an `ElfWriter` beside `PEWriter`, and a System V calling convention (different argument registers, a red zone, no shadow space). |
| **macOS / Mach-O** | Research | Also needs ARM64, which is a second backend. |
| **ARM64** | Research | A full second instruction encoder, with the same ground-truth harness. |

---

## How to help

Every phase has entry points that do not require knowing x86-64:

- **Phase 1** is compiler work, and the hardest.
- **Phases 3-5** are mostly **written in Wandaa**, not C++. If you can write
  Wandaa and read a C API reference, you can write the SQLite binding.
- **Documentation and translation** matters as much as code for a language
  whose point is accessibility.

See [CONTRIBUTING.md](CONTRIBUTING.md), and [GOVERNANCE.md](GOVERNANCE.md) for
how design decisions get made.
