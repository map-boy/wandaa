# Contributing to Wandaa

Wandaa is a compiled programming language with Kinyarwanda keywords, compiling
straight to native x86-64 Windows executables. Contributions are welcome.

## Getting started

1. Fork and clone the repo.
2. Build the compiler:
   ```powershell
   .\build.ps1 examples\mbere.waa
   ```
3. This compiles `mbere.waa` and runs the resulting executable.

## Project layout

- `src/` — compiler source (lexer, parser, codegen)
- `include/` — headers, including the PE writer and x86-64 encoder
- `examples/` — sample `.waa` programs, also used as smoke tests in CI
- `editors/` — editor extensions (currently VS Code / VSCodium syntax highlighting)

## Ways to contribute

- **Language design** — propose new keywords or syntax by opening an issue first, so the grammar stays consistent.
- **Compiler internals** — lexer/parser/codegen improvements. Run all files in `examples/` before opening a PR.
- **Standard library / builtins** — new built-in functions live in `codegen.cpp`'s builtin table.
- **Tooling** — editor extensions, package manager, debugger.
- **Documentation** — the language spec, examples, tutorials.

## Pull requests

- Keep PRs focused on one change.
- Make sure `.\build.ps1` succeeds for every file in `examples/` before submitting.
- Describe *why* the change is needed, not just what changed.

## Code of conduct

Be respectful. Disagreements about design are fine; personal attacks are not.
