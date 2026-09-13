# Imbere muri Wandaa — Compiler Internals

How `wandaac` turns a `.waa` file into a Windows executable, and why it is
built the way it is.

---

## The pipeline

```
.waa source
   -> lexer     (src/lexer.cpp)      characters -> tokens
   -> parser    (src/parser.cpp)     tokens -> AST
   -> modules   (src/modules.cpp)    resolve `injiza`, splice modules in
   -> inference (src/codegen.cpp)    infer int / string / array per name
   -> codegen   (src/codegen.cpp)    AST -> machine code + PE image
   -> .exe
```

There is **no assembler and no linker** in that list. `wandaac` writes the
executable itself. That is the single most important property of the design:
an end user installs one file.

---

## Why no external toolchain

The original backend emitted GNU-assembler text and shelled out to
`gcc -nostdlib -nostartfiles`. It worked, but it meant every Wandaa user had to
install MinGW — a larger download than the compiler, on a platform where
installing a Unix toolchain is the hardest step. For a language meant to lower
the barrier to programming in Rwanda, that barrier was the whole problem.

Two pieces removed it:

- `include/pe_writer.hpp` writes a minimal PE64 image directly: DOS stub, COFF
  header, optional header, one section, and a hand-built import table.
- `include/x64asm.hpp` encodes the instructions the code generator needs.

---

## The runtime blob

Roughly 150 instructions of Windows glue — `WriteFile` plumbing, integer
formatting, string concatenation, file I/O, the crash handler, `_start` — are
identical in every compiled program. Hand-encoding them would have been the
most error-prone part of the whole project.

Instead they live in `tools/runtime/runtime.s` as ordinary assembly, are
assembled **once at compiler build time** by `tools/build_runtime.sh`, and are
extracted into `include/runtime_blob.hpp` as a byte array plus fixup tables.
End users never run an assembler; the compiler's own build does.

Two properties make this work:

**1. The blob is relocatable by `memcpy`.** Code and data both live in `.text`,
so every internal reference is RIP-relative and both ends move together. GNU
`as` resolves those at assembly time and emits no relocation at all.
`tools/extract_blob.cpp` *enforces* this: a relocation against a symbol defined
inside `.text` is a hard error, not a warning.

**2. Every import is a named, recorded fixup.** Windows API calls are written
as `call [rip+__imp_WriteFile]`, leaving `__imp_WriteFile` undefined, so the
assembler emits exactly one relocation naming it. The extractor records the
byte offset; the code generator repoints it at `PEWriter`'s own IAT slot. The
old linker did this for us — now it is explicit and checkable.

Anything else undefined is a bug, and extraction fails loudly. The one
deliberate exception is `wandaa_main`, the single edge from `_start` into the
code the compiler generates.

---

## Section layout

`PEWriter` emits one RWX section. Everything is placed at section-relative
offsets, which is the coordinate system `PEWriter`, `X64Asm` and the blob
fixups all share:

```
+------------------+ 0
| import block     |  descriptors, IAT slots, name tables
+------------------+ blobBase   (16-byte aligned)
| runtime blob     |  the fixed runtime, memcpy'd verbatim
+------------------+ codeBase
| generated code   |  user functions, then wandaa_main
+------------------+ dataBase   (8-byte aligned)
| string literals  |  8-byte length header, bytes, NUL
+------------------+
```

Three kinds of cross-region reference:

| From | To | Mechanism |
|---|---|---|
| generated code | runtime | `defineAbsLabel(blobBase + offset)`, then `call rel32` |
| generated code | IAT | `defineAbsLabel(iatSlotOffset)`, then `call [rip+slot]` |
| runtime | generated code | `_start`'s `call wandaa_main`, patched from `CODE_FIXUPS` |

Data lives in the same section as code because the section is RWX. That is
unusual for a production linker and worth naming as a deliberate tradeoff: it
keeps the image to a single section and makes the blob position-independent,
at the cost of a writable, executable section. Splitting `.text` from `.data`
(and dropping the W bit) is on the roadmap.

---

## Data representation

**Strings** are a pointer to their bytes with an 8-byte length header
immediately before. `uburebure()` is `mov rax, [rcx-8]`. The bytes are also
NUL-terminated, so a Wandaa string can be passed straight to a Windows `...A`
function — which is what makes FFI usable without marshalling.

**Arrays** use the same shape: an 8-byte element count, then the elements, with
the value pointing at the first element. Heap-allocated via `GetProcessHeap` +
`HeapAlloc`.

There is currently **no deallocation**. Concatenating strings in a long loop
grows the process heap until it exits. This is a real limitation, not an
oversight — see the roadmap.

---

## Calling convention

Windows x64, followed exactly:

- Arguments 0-3 in RCX, RDX, R8, R9; the rest in the caller's outgoing area.
- 32 bytes of shadow space, caller-allocated and caller-cleaned.
- RSP 16-byte aligned at every `CALL`.

`emitCall` reserves the whole outgoing area once and stores argument *i* at
`[rsp + 8*i]` — which is exactly where the ABI wants it, since the shadow space
is the home for the first four. Arguments five and up need no shuffling,
left-to-right evaluation order is preserved, and alignment holds because a
single aligned amount is reserved before any argument is evaluated. Callees
reload stack arguments from `[rbp + 16 + 8*i]`.

The alignment bookkeeping is not decorative. Expression evaluation pushes
temporaries, so mid-expression RSP sits at an odd multiple of 8; the code
generator tracks outstanding temporaries and pads. Without that, `f(a, g(b))`
calls `g` misaligned, and any DLL using SSE faults on a `movaps`.

---

## How correctness is checked

Nothing here is asserted by inspection.

**Encoder ground truth.** `tools/enc/verify_enc.cpp` emits the same ~7,500
instruction sequence twice — once as GAS text, once through `X64Asm` — then
`run_verify.sh` assembles the text with the real GNU assembler and diffs the
bytes. Every one must match. Coverage is deliberately aimed at what actually
goes wrong in x86-64 encoding: all 16 registers in every operand position, the
RSP/R12 mandatory-SIB case, the RBP/R13 `mod=00` case, every disp and immediate
boundary, RIP-relative with a trailing immediate (where the displacement is
measured *past* the immediate), and rel32 branches in both directions with
targets padded beyond `rel8` range so the assembler is forced to agree.

**Blob freshness.** CI regenerates `runtime_blob.hpp` from `runtime.s` and
fails if it differs from the committed copy.

**Differential testing.** Every program the old `gcc` pipeline can still
compile is built by both backends and run; stdout and exit codes must be
byte-identical.

**The dependency itself.** CI compiles a program on Windows with `PATH`
stripped to the system directories, so `gcc`, `as` and `ld` are unreachable,
and asserts the binary still runs. `wandaac.exe` is statically linked so it
carries no MinGW runtime DLL dependency either.

---

## Reading the generated code

```powershell
.\wandaac.exe examples\gito.waa gito.exe
objdump -d -M intel gito.exe          # if you have binutils, for inspection
objdump -p gito.exe                   # import table
```

The blob bytes in the `.exe` are identical to `objdump -d runtime.o` from
`runtime.s` except at the recorded fixup sites. That is a check you can run by
hand when something looks wrong.
