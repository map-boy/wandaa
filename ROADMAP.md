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
- 21 end-to-end tests, differential-tested against the previous backend

**The three limitations that block the most:**

1. **No floating point.** Blocks statistics, graphics, money-with-cents, and
   all of ML.
2. **No memory reclamation.** Blocks long-running servers.
3. **No records/structs.** Blocks ergonomic libraries and self-hosting.

Everything in Phase 1 below exists to remove those.

---

## Phase 1 — Language completeness

The prerequisite for everything else. Nothing downstream is worth starting
before this lands.

| Item | Status | Notes |
|---|---|---|
| **Floating point (`f64`)** | Designed | Needs SSE2 in the encoder (`movsd`, `addsd`, `mulsd`, `divsd`, `cvtsi2sd`, `cvttsd2si`, `ucomisd`), XMM0-3 in the calling convention, a `VType::Float`, and float literals in the lexer. The encoder's ground-truth harness extends to cover it the same way. |
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
| **`wandaa` CLI** | Planned | One entry point: `wandaa tangira` (new), `yubaka` (build), `koresha` (run), `gerageza` (test), `hindura` (fmt). `wandaac` stays as the bare compiler underneath. |
| **Package manager** | Designed | See below. |
| **Formatter** | Planned | Reuses the existing parser; prints the AST. One canonical style, no options. |
| **Test runner** | Planned | `gerageza` blocks in source, collected and run by `wandaa gerageza`. |
| **Language server (LSP)** | Planned | Completion, go-to-definition, inline errors. The parser already tracks line numbers. |
| **Debugger support** | Research | Requires emitting PDB or DWARF, and splitting `.text` from `.data` first. |

### Package manager design

A manifest, `wandaa.toml`:

```toml
[umushinga]              # project
izina = "urubuga-rwanjye"
verisiyo = "0.1.0"

[ibisabwa]               # dependencies
json = "1.2"
sqlite = { git = "https://github.com/...", tag = "v0.3" }
```

- Resolution produces `wandaa.lock` pinning exact versions and content hashes.
- Dependencies vendor into `ibipapuro/` (packages), which is added to the
  `injiza` search path — the module system already supports this.
- The registry is a static index, so it can be mirrored and works offline.
  **This matters more than it sounds**: a package manager that assumes reliable
  broadband is not usable everywhere it needs to be. Offline install from a
  local mirror is a requirement, not a nice-to-have.
- Signed packages, and a vendored-source mode with no network at all.

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

The honest position: **this needs floating point first**, and floating point is
Phase 1. Everything below is designed but genuinely blocked, and claiming
otherwise would waste a contributor's time.

| Item | Status | Notes |
|---|---|---|
| **`f64` arithmetic** | Designed (Phase 1) | Hard prerequisite. |
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
