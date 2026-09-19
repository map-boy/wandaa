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
- **Records (`ubwoko`)** with declared byte widths, so a record can match a
  Win32 struct byte for byte
- **Pointers** — `&x`, enough to pass a struct or an out-parameter to a DLL
- **Native sockets from Wandaa**: bind, listen and getsockname over
  `ws2_32.dll`, confirmed against the real Windows kernel
- **`waa file.waa`** — compile and run in one step, output cached out of sight
- 26 end-to-end tests, differential-tested against the previous backend

**The limitations that block the most:**

1. **The type checker does not check.** `evalType` falls back to `VType::Int`
   in nine places, including for an unknown variable, so `reka x = izinaRitariho;`
   compiles silently. There is no way to write a type down at all. Every
   validation, schema and ORM feature further down this file is worthless until
   this fails loudly instead.
2. **No memory reclamation.** Blocks long-running servers.

Floating point and records were on this list and are now **Done**. Item 1 is
new to the list rather than newly true: it has always been the case, and became
the binding constraint once records made real data structures possible.

---

## Phase 1 — Language completeness

The prerequisite for everything else. Nothing downstream is worth starting
before this lands.

| Item | Status | Notes |
|---|---|---|
| **Floating point (`f64`)** | **Done** | SSE2 in the encoder (all forms diffed against GNU `as` across XMM0-15), `VType::Float`, float literals, XMM0-3 in the calling convention, and 6-decimal trimmed printing in the runtime. Integer division is unchanged: `7 / 2` is still 3, and promotion happens only when an operand is already a float. NaN compares false against everything including itself. See WDP #2. Not yet: reading a `double` RETURNED by a `hanze` function, which Win64 passes back in XMM0 rather than RAX. |
| **Type checking that fails** | **Partial** | A semantic pass now runs before any code is emitted and rejects five things outright: wrong argument count to a function, wrong field count to a record constructor, a field name the record does not have, arithmetic other than `+` on a string, and `+` between a string and a value that is *certainly* not one. It also rejects an argument whose type is certainly wrong for a DECLARED parameter type (see the row below), and an unknown type name. It deliberately stops there. `VType::Int` is still the inference fallback, so a value whose type could not be worked out passes — the pass only rejects what it can prove wrong, because a false positive rejects a working program, and only DECLARED parameter types are checked, never inferred ones. Unknown identifiers and unknown function names already errored before this pass and still do. What is left for Full: checking is per-call, so a function body that misuses its own declared parameter is not caught. |
| **Type annotations** | **Done** | `izina: ijambo` on parameters, returns, record fields and `reka`. Types: `umubare`, `ibice`, `ijambo`, `urutonde`, `igisubizo`, or a declared record name; `urutonde<ibice>` and `igisubizo<ijambo>` also say what the elements or payload are. Every annotation is optional and a missing one leaves inference exactly as it was, so no existing file changes meaning; a present one wins over inference. In a record, a number after `:` is still a byte width and a name is a type — they never collide. This is what removes the guesswork the previous three rows had to live with: it fixes the closure-as-parameter limit, the array element type and the result payload type. |
| **Records (`ubwoko`)** | **Done** | Named fields over a heap block; field access is a constant offset resolved at compile time. Fields take an optional declared byte width (`family:2`), packed with no alignment padding, so a record matches a C struct byte for byte. Verified by disassembly and by Windows itself reading one back. |
| **Pointers (`&x`)** | **Done** | `&x` on an integer or float local is its frame slot; on a record, array or string it is the heap pointer already held there. Enough to pass structs and out-parameters to `hanze` functions. Not general pointer arithmetic, and deliberately so. |
| **Closures** | **Done** | `umurimo (x) { ... }` with no name is a value: it goes in a variable, into an array, into another function and back out. It captures the enclosing variables it uses **by value**, which is a decision rather than a shortcut — a closure can outlive the call that made it, and with no reference counting a captured *variable* would point into a dead frame. Compiled by lifting: the body becomes an ordinary top-level function and the closure is a heap block holding its code pointer plus a copy of each capture, passed as a hidden first argument. Needed `call r64`, now verified against GNU `as` across all 16 registers. Limits: a closure received as a *parameter* has no known return type, so a string it returns prints as a pointer; and an anonymous function cannot call itself, which is rejected at compile time with the fix in the message. |
| **Generics (functions)** | **Done** | `umurimo mbere<T>(a: urutonde<T>): T` — one function over every element type. It needs **no monomorphisation**: every Wandaa value is 8 bytes in a register, so one body serves every `T` and generics are purely a compile-time device for working out what a call gives back. A parameter whose type *is* the type parameter is pinned to the integer register file and passed raw, with no coercion — otherwise the one settled parameter type would convert a float argument and destroy it. Inside the body `T` is opaque, which is correct: the code there only moves 8 bytes around. Binds from `x: T`, `urutonde<T>` and `igisubizo<T>`, first mention wins. |
| **Generics (records)** | Planned | `ubwoko Ikibiga<T> { agaciro: T }` needs a field type that varies per instance, which the current per-type `RecordTypeInfo` cannot express. Functions were the valuable half and shipped first. |
| **`for` loops** | Planned | `kuri i muri 0..n` — desugars to the existing `mugihe`. |
| **Bounds-checked indexing** | **Done** | Every read and every write through `a[i]` is checked against the array's 8-byte count header. Out of range stops the program with the source line, the index and the length, and exit code 1. The comparison is *unsigned* (`jb`), so one branch catches both a negative index and one past the end — a negative wraps to a huge value and fails the same test. Applies to literal and `urutonde()` arrays alike. No opt-out yet: correctness first, and the check is a load, a compare and a not-taken branch. |
| **Memory reclamation** | **Partial** | Step one is in: a local is freed when the compiler can PROVE that every value it held was a fresh allocation and that the value never left the variable. Proven locals are released when overwritten and at every exit from the frame, so a loop that builds one string per request now reuses a handful of blocks instead of growing forever (`tests/cases/memory.waa` counts distinct addresses: 200 iterations, under 20 addresses; the same loop with the value stored elsewhere keeps climbing, which is the negative control). Anything unprovable is left exactly as it was, so the failure mode is a leak and never a use-after-free. Not done: full reference counting. That needs to know which of a block's 8-byte slots are pointers, and `Int` is still the inference fallback — a slot whose type was never worked out would be decremented as if it were a pointer, and getting that wrong frees a live object. Nothing recurses into a block yet either, so an array's elements outlive the array. |
| **Borrow checking** | **Declined** | Considered and deliberately not adopted. It fails GOVERNANCE principle 3 — lifetime and region inference is the largest single thing a reader of this compiler would have to hold in their head, and "small enough that a motivated student can read the whole compiler" is a constraint this project chose on purpose. It also fails principle 5: a borrow checker rejects programs that work today, and there is no migration path for a `.waa` file whose aliasing simply is not expressible. The memory-safety goal is met instead by the bounds checking above plus the refcounting row, which together cover the failures that actually bite a data or server program: out-of-range access and leaks. Revisit only if Wandaa acquires threads, where aliasing stops being hygiene and becomes correctness. |
| **Optional / missing values** | Planned | A real `ntacyo` rather than a silent 0 or empty string. This is where data pipelines break in practice. The machinery now exists — an optional is a result whose failure carries no message — but it still needs the type-annotation row above to be worth declaring. |
| **Error values** | **Done** | `igisubizo` — a result is either `byakunze(v)` (success) or `byanze(ubutumwa)` (failure carrying a message), inspected with `byarakunze()`, `agaciro()` and `ikosa()`. Postfix `?` unwraps a success and returns a failure straight to the caller, so fallible calls chain without losing errors; at top level it reports the message and exits 1. The reported line is where the failure was *created*, not where it was propagated. Represented as a two-slot heap block in the same shape as an array (count header, tag, payload), so it needed no new allocator. Limit: with no generics the payload's type is inferred, not declared, and falls back to integer when it cannot be worked out. |
| **`hanze` returning a double** | Planned | Win64 returns a `double` in XMM0; Wandaa reads RAX, so such a function reads wrong today. Passing doubles in already works. |
| **Multi-file line numbers** | Planned | `wandaa_current_line` is a single slot; with modules a fault should name the file too. |
| **UTF-8 aware strings** | Planned | `inyuguti()`/`igice()` work on bytes. Kinyarwanda is ASCII-representable, but the strings a program *handles* are not necessarily. |

---

## Phase 2 — Tooling

| Item | Status | Notes |
|---|---|---|
| **`waa file.waa`** | **Done** | Compile and run in one step, like `python file.py`. The binary is cached in `.waa_cache/` beside the source — the `__pycache__` idea — and reused until the source is newer, so no build output lands in a working directory. Compile errors print under their own header, separate from program output. |
| **`wandaa` CLI** | **Done** | `tangira` (new), `ongeraho` (add a dependency), `shakisha` (fetch), `genzura` (verify), `yubaka` (build), `koresha` (run), `gerageza` (test), `shyiraho`/`hindura` (file create/append), `serivisi`/`hagarika` (static HTTP server), `verisiyo`. `wandaac` stays as the bare compiler underneath. |
| **Regression guard** | **Done** | `tools/guard.ps1` builds, runs the suite and diffs pass/fail against `.guard/baseline.json`, blocking only on a real regression — something that used to pass and now fails. `-Baseline` accepts a new normal; `-Wandaac` points it at another compiler build, which is how a self-hosted stage will be checked. |
| **Package manager — core** | **Done** | `wandaa.toml` manifest, local-path and git dependencies, vendoring into `ibipapuro/`, `wandaa.lock` with a SHA-256 over each package, and `genzura` to verify it. Builds are offline once `ibipapuro/` exists. |
| **Package manager — registry** | Planned | Today a dependency is a local path or a git URL. A named, versioned registry with semver resolution does not exist yet. See below. |
| **Test runner** | **Done** (file-based) | `wandaa gerageza` compiles and runs every `.waa` in `tests/`; a test passes when it exits 0. In-source `gerageza` blocks are still Planned. |
| **Formatter** | Planned | Reuses the existing parser; prints the AST. One canonical style, no options. Note `hindura` is already taken by the file-append command, so the formatter needs a different name. |
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
| **SQLite driver** | Designed | `hanze "sqlite3.dll"` over `sqlite3_open`, `_prepare_v2`, `_step`, `_column_*`, `_finalize`. **Unblocked**: records now exist for row values, and `&x` can pass the out-parameters `sqlite3_open` needs. This is the next thing anyone could pick up. |
| **PostgreSQL driver** | Planned | Same approach via `libpq.dll`. |
| **ODBC** | Planned | Reaches SQL Server and anything else with a driver. |
| **Connection pooling** | Planned | Needs memory reclamation. |
| **ORM (`ubutunzi`)** | Designed | Records map to tables; a query builder produces parameterised SQL. Parameterised from day one — a language taught to beginners should make SQL injection hard to write by accident. Migrations generated from record definitions. |
| **CSV / JSON** | Planned | Pure Wandaa, in `lib/`. Records exist now, so the blocker is the type system: parsing into a declared shape is the point, and today nothing would reject a wrong one. |

---

## Phase 4 — Web and servers

| Item | Status | Notes |
|---|---|---|
| **Sockets** | **Done** | `WSAStartup`, `socket`, `bind`, `listen`, `getsockname`, `closesocket` called natively from `.waa` over `ws2_32.dll`, with `sockaddr_in` as an `ubwoko` record. `getsockname` reads the port back out of the kernel, which is what proves the byte layout. See `examples/net/sockets.waa`. Still to add: `accept`, `recv`, `send`, and a byte buffer to receive into. |
| **Concurrency: threads, not async** | Planned | FastAPI is async because CPython is slow and has a GIL — threads cannot run Python bytecode in parallel. Wandaa compiles to native code with neither constraint, so thread-per-connection over `CreateThread` uses every core with no event loop, no coloured functions and no async driver ecosystem. Copying `async`/`await` would import Python's hardest complexity to solve a problem this language does not have. |
| **Byte buffers** | Planned | `recv` needs somewhere to write bytes. Arrays are 8-byte slots, which makes every HTTP parse loop fight the representation. A raw byte buffer type is the prerequisite for reading a request at all. |
| **HTTP/1.1 parser** | Planned | Pure Wandaa. |
| **Server framework (`urubuga`)** | Designed | Routing, request/response records, middleware, written in Wandaa and imported with `injiza`. Records exist now; the remaining blocker is memory reclamation — a server that leaks per request is not a server. A static-file server already ships as `wandaa serivisi`, but that is C++ inside the CLI, not Wandaa, and is a stopgap rather than the goal. |
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
| **Tensors (`ikibumbano`)** | Designed | N-dimensional f64 array with shape metadata; strided views. **Unblocked** by f64 and records. For this to catch shape and dtype mistakes rather than corrupt data, shape belongs in the type — which is the type-system row in Phase 1. |
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

- Records — an AST is a tree of tagged nodes. **Done**
- Growable arrays — **Done**: `ongeraho(a, x)` appends and returns the array,
  with capacity in a second header word and doubling growth
- A map type — **Done**: `inkoranya()` with `shyiramo` / `fata` / `arimo`,
  open addressing over string keys, rehashing as it fills
- String building that does not leak — the code generator produces a lot of it
- File I/O — already present
- Enough recursion depth for deeply nested expressions

**The bootstrap sequence:**

| Stage | What | Status |
|---|---|---|
| 0 | The current C++ compiler | **Done** |
| 1 | Phase 1 language features, so a compiler is writable | **Prerequisites met** |
| 2a | `compiler/lexer.waa` — the lexer in Wandaa | **Done** |
| 2b | `compiler/parser.waa` — the parser in Wandaa | Planned |
| 2c | `compiler/codegen.waa` — the code generator in Wandaa | Planned |
| 3 | Compile stage 2 with stage 0 → `wandaac-s1.exe` | Planned |
| 4 | Compile stage 2 with `wandaac-s1.exe` → `wandaac-s2.exe` | Planned |
| 5 | **Assert `wandaac-s1.exe` and `wandaac-s2.exe` are byte-identical** | Planned |

Stage 2a is in. `compiler/lexer.waa` is the Wandaa lexer, written in Wandaa,
and `compiler/verify_lexer.sh` (with a PowerShell twin for CI) requires its
token stream to be byte-identical to `wandaac --tokens` for every `.waa` file
in the repository — 49 of them, including its own source. That is the same
ground-truth-by-comparison the instruction encoder uses, and it is a CI gate.

Every feature listed above as required now exists. That is not the same as
having proved a compiler is writable in Wandaa — only stage 2 proves that, by
being written — so stage 1 says "prerequisites met" rather than "done".

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
