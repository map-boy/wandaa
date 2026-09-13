# Guhindura Wandaa — Updating Wandaa

How a change to the compiler, the instruction encoder, the runtime blob, or the
standard library actually gets made, verified and merged.

This document is about **mechanics**. Who may merge and how design decisions are
reached is [GOVERNANCE.md](../GOVERNANCE.md); where to start as a newcomer is
[CONTRIBUTING.md](../CONTRIBUTING.md); how the compiler works internally is
[docs/imbere.md](imbere.md). This file assumes you have already decided what to
change and want to know how to change it without breaking the build.

---

## 1. Branching and pull requests

Work happens on branches off `main` and lands by pull request. `main` is the
only long-lived branch; there is no develop branch and no release branch yet.

```bash
git fetch origin main
git checkout -b izina-ry-impinduka origin/main
# ... work ...
git push -u origin izina-ry-impinduka
```

Three rules that are specific to this repo rather than general advice:

- **A merged pull request is finished.** If follow-up work is needed after a
  merge, cut a new branch from the updated `main`. Do not push new commits onto
  a branch whose PR has already merged — it cannot reopen, and the commits will
  sit unreviewed.
- **Generated files are committed, and CI checks they are current.**
  `include/runtime_blob.hpp` and `docs/isomero.md` are both generated. If your
  change affects either, regenerate and commit the result in the same PR.
  Sections 3, 4 and 6 below say exactly when.
- **One change per pull request.** The encoder, the runtime blob and the
  standard library each have their own verification path; mixing them makes a
  failure harder to attribute.

---

## 2. What CI checks, and how to pass it locally

CI is `.github/workflows/build.yml`, three jobs. Every one must be green before
a PR merges.

### `windows` (windows-latest)

1. Builds `wandaac.exe` and `wandaa.exe` with `g++ -std=c++17 -O2 -Wall -Wextra -static`.
   The `-static` is load-bearing: without it the binaries need
   `libstdc++-6.dll` and `libgcc_s_seh-1.dll`, and step 2 fails.
2. **Self-containment check.** Compiles `examples\gito.waa` with `PATH` stripped
   to `%SystemRoot%\System32;%SystemRoot%`, so `gcc`, `as` and `ld` are
   unreachable, then runs the result and asserts it prints `1`. This is the
   regression test for Wandaa's central claim — that end users need no
   toolchain. If you ever add a `system()` call or a shell-out to the compile
   path, this job is what catches it.
3. Runs `.\tests\run_tests.ps1`.

### `verify` (ubuntu-latest, needs `binutils-mingw-w64-x86-64`)

1. **Encoder ground truth** — `./tools/enc/run_verify.sh`. See section 3.
2. **Standard library docs freshness** — regenerates `docs/isomero.md` with
   `python3 tools/gen_stdlib_docs.py` and fails if `git diff` is non-empty.
3. **Runtime blob freshness** — regenerates `include/runtime_blob.hpp` with
   `./tools/build_runtime.sh` and fails if `git diff` is non-empty. See
   section 4.

### `linux-wine` (ubuntu-latest, needs `wine64`)

1. `./build.sh`
2. `./tests/run_tests.sh` — compiles every program in `tests/cases/` and
   `examples/`, runs it under Wine, and compares stdout **and exit code**
   against the frozen files in `tests/expected/`.
3. `./tests/test_cli.sh` — the `wandaa` project tool end to end: scaffold, add
   a dependency, vendor it, check the lockfile hash against `sha256sum`, build
   against the vendored module and the standard library, run the project's
   tests, and confirm a tampered package is rejected.

### Running the same checks locally

On Linux with Wine and `binutils-mingw-w64-x86-64` installed, you can run every
CI check except the Windows-native ones:

```bash
./build.sh                      # build wandaac and wandaa
./tests/run_tests.sh            # the language test suite, under Wine
./tests/test_cli.sh             # the project/package tool
./tools/enc/run_verify.sh       # encoder vs GNU as
./tools/build_runtime.sh && git diff --exit-code include/runtime_blob.hpp
python3 tools/gen_stdlib_docs.py && git diff --exit-code docs/isomero.md
```

On Windows:

```powershell
.\build.ps1 examples\mbere.waa   # build, then compile and run one program
.\tests\run_tests.ps1            # the language test suite, natively
```

`WANDAA_RUNNER=wine` tells the `wandaa` project tool how to launch a produced
`.exe` when you are cross-developing; `tests/test_cli.sh` sets it for you.

### Adding a test

Anything that changes behaviour needs a case. Tests are plain programs plus
frozen output:

```bash
# write tests/cases/ikintu.waa, then freeze what it currently prints
./bin/wandaac tests/cases/ikintu.waa /tmp/ikintu.exe
( cd /tmp && wine ./ikintu.exe > ikintu.out 2>/dev/null; echo $? > ikintu.code )
cp /tmp/ikintu.out  tests/expected/ikintu.out
cp /tmp/ikintu.code tests/expected/ikintu.exit
```

Freeze the output only after you have read it and confirmed it is correct.
Recording whatever the compiler currently prints turns a test into a snapshot of
a bug.

Keep test output deterministic: no timestamps, no process ids, no tick counts.
`tests/cases/ffi.waa` shows the pattern — it calls `GetCurrentProcessId()` and
`GetTickCount()` but only prints range checks on them, never the values.

---

## 3. Adding an instruction to `include/x64asm.hpp`

The encoder is never trusted on inspection. Every instruction form it emits is
diffed byte-for-byte against GNU `as`, and that diff is a CI gate.

### How the harness works

`tools/enc/verify_enc.cpp` emits the **same instruction sequence twice**:

- `--asm out.s` — as GNU-as Intel-syntax text
- `--bin out.bin` — as bytes, through `X64Asm`
- `--map out.map` — a table of encoder byte offset → instruction text

`tools/enc/run_verify.sh` then assembles the `.s` with
`x86_64-w64-mingw32-as`, extracts `.text` with `objcopy -O binary`, and
compares it to the encoder's bytes. Trailing bytes from the assembler are
allowed only if they are `0x90`/`0x00` alignment padding; anything else fails.
On a mismatch the script uses the map to name the instruction the first
differing byte falls inside, so you get "in instruction: `add rax, 128`" rather
than an offset.

### The procedure

1. **Add the method to `include/x64asm.hpp`.** Follow the existing private
   helpers rather than hand-rolling ModRM: `memBaseDisp` covers `[base + disp]`
   including the two quirks that make x86-64 addressing unforgiving (RSP/R12
   need a mandatory SIB byte; RBP/R13 cannot use `mod=00`, which means
   RIP-relative), and `memSib` covers `[base + index*8]`.

2. **If it is RIP-relative, get `tail` right.** A RIP displacement is measured
   from the *end of the instruction*. For most forms the disp32 field is last,
   so `tail = 0`. For `mov qword ptr [rip+X], imm32` four immediate bytes
   follow the field, so `tail = 4`. Passing 0 there produces an off-by-four
   that no amount of reading the code will reveal — the harness will.

3. **Add cases to `tools/enc/verify_enc.cpp`.** Use `h.note("<gas text>")`
   immediately before the encoder call, so the map stays aligned:

   ```cpp
   for (int d = 0; d < 16; ++d)
       for (int s = 0; s < 16; ++s) {
           h.note(std::string("mov ") + RN[d] + ", " + RN[s]);
           h.a.mov_reg(R(d), R(s));
       }
   ```

   Cover the cases that actually go wrong, not a happy path: **all 16
   registers in every operand position** (that is what exercises REX.R/X/B),
   RSP and R12 as a base, RBP and R13 as a base, and every
   disp0/disp8/disp32 and imm8/imm32 boundary. The existing `DISPS` and `IMMS`
   arrays already straddle those boundaries — reuse them.

4. **Run it.**

   ```bash
   ./tools/enc/run_verify.sh
   ```

   Expect `ENCODER OK: N instruction bytes match GNU as byte-for-byte`.

### Two traps worth knowing in advance

**GNU `as` picks the shortest encoding, and so must you.** For `add rax, 128`
the assembler emits the 6-byte accumulator form `48 05 id`, not the 7-byte
`48 81 C0 id`. A correct-but-longer encoding still fails the diff. `grp1Imm`
already implements the shortest-first choice; match that style.

**Branches cannot be compared naively.** `X64Asm` always emits `rel32` — a
single-pass encoder cannot know a forward jump's distance when it emits the
opcode — while `as` runs multiple passes and shortens anything within `rel8`
range. `near ptr` does not stop it. The harness therefore pads branch targets
beyond ±128 bytes with `.space`, which forces `as` to emit `rel32` too. If you
add a branch form, pad it the same way; otherwise you are comparing a 2-byte
instruction against a 5-byte one.

---

## 4. Changing the runtime blob

`tools/runtime/runtime.s` holds the part of every compiled program that never
varies: `WriteFile` plumbing, integer formatting, string concatenation, file
I/O, the crash handler, `_start`. It is assembled **once, at compiler build
time**, and shipped as bytes in `include/runtime_blob.hpp`. End users never run
an assembler.

### What makes the blob relocatable, and what enforces it

Code and data both live in `.text`, so every internal reference is RIP-relative
and both ends move together — GNU `as` resolves them at assembly time and emits
no relocation at all. That is what lets the code generator drop the blob into a
PE section with a plain `memcpy`.

`tools/extract_blob.cpp` **enforces** this rather than assuming it. It fails
the build on:

- any relocation against a symbol defined inside `.text` (the blob would break
  when relocated)
- any relocation type other than `IMAGE_REL_AMD64_REL32`
- a non-empty `.data` or `.bss` section
- an undefined symbol that is neither `__imp_<WinAPI>` nor a declared external
  code symbol
- a Windows import not listed in `kImportDll`
- a missing required label

### How imports get repatched

Windows API calls in `runtime.s` are written as indirect calls through an
undefined placeholder:

```asm
call qword ptr [rip+__imp_WriteFile]
```

Leaving `__imp_WriteFile` undefined makes the assembler emit exactly one
`REL32` relocation naming it. The extractor records the byte offset of the
disp32 field and the DLL the function lives in, and emits it into
`IMPORT_FIXUPS[]`. At compile time `src/codegen.cpp` repoints each site at
`PEWriter`'s own IAT slot:

```
field = iatSlotOffset + addend - (blobBase + site + 4)
```

The old linker used to do this for us; it is now explicit, and therefore
checkable. `CODE_FIXUPS[]` handles the one reference the other way — `_start`'s
`call wandaa_main` into the code the compiler generates.

### The procedure

1. **Edit `tools/runtime/runtime.s`.**

   - Windows API calls go through `call qword ptr [rip+__imp_NAME]`, never a
     direct `call NAME`.
   - New API? Add `NAME → dll` to `kImportDll` in `tools/extract_blob.cpp`,
     or extraction fails with "unknown Windows import".
   - New entry point the compiler will call? Give it a `.globl`, and add it to
     the required-labels list in `tools/extract_blob.cpp` so a missing `.globl`
     is caught at build time rather than showing up as a bad call target.
   - Keep data at the **top** of the file, before the code. GNU `as` resolves a
     forward-referenced absolute symbol as an *address*, which silently turns
     `mov edx, wandaa_crash_msg_len` into a memory load from `0x12`. Defining
     it above its use keeps it an immediate.
   - Mind the Win64 ABI inside the runtime too: RSP 16-byte aligned at every
     `call`, 32 bytes of shadow space, and RBX/RSI/RDI/R12-R15 preserved.
     `rep movsb` clobbers RSI and RDI, which are callee-saved — push them.

2. **Regenerate and commit.**

   ```bash
   ./tools/build_runtime.sh          # or tools\build_runtime.ps1 on Windows
   git add include/runtime_blob.hpp
   ```

   The script assembles `runtime.s`, builds the extractor, and rewrites the
   header. Its output line — bytes, import fixups, code fixups, labels — is a
   quick sanity check that your new function was picked up.

3. **Wire it into the language**, if it is a new builtin. In
   `src/codegen.cpp`, add the Kinyarwanda name to the `builtins` map in
   `genCall`, and if it returns a string, register that in `g_fnReturnTypes`
   inside `generate()` so type inference knows.

4. **Test it, then run the suite.** A runtime function is reachable from
   Wandaa, so it gets a normal test case under `tests/cases/`.

### How CI catches drift

The `verify` job re-runs `tools/build_runtime.sh` and fails if
`include/runtime_blob.hpp` differs from the committed copy. So an edit to
`runtime.s` without a regenerate is caught, and — more importantly — so is a
hand-edited blob. Pre-built machine code checked into a repository is an
obvious place to hide something; reviewers review `runtime.s`, and CI proves
the bytes correspond to it.

---

## 5. Moving a ROADMAP item to Done

[ROADMAP.md](../ROADMAP.md) is the single source of truth for feature status —
[README.md](../README.md) deliberately links to it instead of repeating it. A
status change is a claim about the repository, so it carries evidence.

**An item moves to Done in the same pull request that makes it true**, never in
a follow-up "update the docs" commit. Two commits means a window where the
roadmap is wrong, and that window is exactly when someone reads it.

Before flipping a status, all of these must hold:

| Requirement | How it is satisfied |
|---|---|
| It works | At least one test in `tests/cases/` exercises it, with frozen expected output |
| It is verified at the right level | New encodings covered in `tools/enc/verify_enc.cpp`; new runtime code regenerated through `tools/build_runtime.sh` |
| It is documented | [docs/ururimi.md](ururimi.md) for language surface, [docs/isomero.md](isomero.md) — regenerated, not hand-edited — for library functions |
| CI is green | All three jobs, on the PR's head commit |

**Partial completion is written down, not rounded up.** If half the item
shipped, split the row rather than marking it Done with a caveat nobody reads.
Phase 2's "Package manager" is the worked example: the core is Done and the
registry is a separate Planned row, because a dependency today is a path or a
git URL and `json = "1.2"` genuinely does not work.

The same applies in reverse. If you find a Done row that is not true, correcting
it is a welcome pull request on its own.

---

## 6. Changing the standard library

`lib/*.waa` is written in Wandaa. No C++ knowledge required, and this is the
best entry point for a first contribution.

1. Edit or add a module in `lib/`.
2. Document each function with a comment **immediately above** it — no blank
   line between. That comment becomes the reference text.
3. Regenerate the reference and commit it:

   ```bash
   python3 tools/gen_stdlib_docs.py
   git add docs/isomero.md
   ```

   Never hand-edit `docs/isomero.md`; the `verify` job regenerates it and fails
   on any difference.
4. Add a test case that uses the new function.

Two things to know about writing Wandaa for the library. The language has **no
`break` out of a nested condition** other than `hagarika`, and working around
its absence with a sentinel index is how a real bug got into the first version
of `gushyira_ku_murongo` — setting the loop index to 0 to leave the loop also
moved the element to the front. Use an explicit `birangiye` flag. And there is
**no memory reclamation**: building a string in a long loop grows the heap until
the process exits, so prefer algorithms that do not concatenate per iteration.

---

## 7. Proposing a language feature

Per [GOVERNANCE.md](../GOVERNANCE.md), some changes need agreement before code.
A **Wandaa Design Proposal (WDP)** is required for:

- new or renamed keywords
- changes to the syntax or grammar
- changes to data representation (how strings or arrays are laid out)
- anything that breaks existing `.waa` programs
- adding a dependency to the compiler build
- changes to the FFI or module resolution rules

Everything else — bug fixes, new standard library functions, new instruction
forms, tests, documentation — needs one committer approval and green CI. That
is the normal path and it should stay boring.

A WDP is an issue using
[the design proposal template](../.github/ISSUE_TEMPLATE/design_proposal.md).
It stays open **at least seven days** so people in other timezones can respond.
Maintainers then accept, reject, or ask for revision, and write down why.

Open the proposal **before** writing the implementation. The most common reason
a proposal is rejected is naming, and naming is settled in discussion, not in a
diff — the seven days cost less than a rewritten weekend.

Proposals are judged against the five design principles in GOVERNANCE.md. Two
of them decide most arguments:

- **Kinyarwanda first.** A new keyword must be a word a Kinyarwanda speaker
  would actually use for the concept, not a transliteration of the English one.
  If you are unsure of the right word, say so and ask — getting it right is
  itself a contribution.
- **A beginner must be able to install it.** Anything that adds an install step
  for end users needs a very strong argument. Removing the MinGW dependency was
  worth a backend rewrite for exactly this reason, and nothing should quietly
  add it back.

---

## Quick reference

| I changed… | I must also… |
|---|---|
| `include/x64asm.hpp` | Add cases to `tools/enc/verify_enc.cpp`; run `./tools/enc/run_verify.sh` |
| `tools/runtime/runtime.s` | Run `./tools/build_runtime.sh`; commit `include/runtime_blob.hpp` |
| `lib/*.waa` | Run `python3 tools/gen_stdlib_docs.py`; commit `docs/isomero.md` |
| `src/codegen.cpp` | Run the encoder verify **and** the full test suite |
| Anything user-visible | Add `tests/cases/*.waa` + `tests/expected/*.out` + `*.exit` |
| A language keyword | Open a WDP first (section 7); update [docs/ururimi.md](ururimi.md) |
| A completed roadmap item | Flip its ROADMAP.md status **in the same PR** (section 5) |
