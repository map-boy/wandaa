## Icyo iyi PR ikora — What this changes

<!-- Sobanura impamvu, atari icyo wahinduye gusa.
     Explain WHY, not just what. The diff already says what. -->

## Ubwoko — Type of change

- [ ] Gukosora ikosa — bug fix
- [ ] Ibishya — new feature
- [ ] Impinduka ku ururimi — language change (needs a design proposal first, see GOVERNANCE.md)
- [ ] Inyandiko — documentation
- [ ] Ibikoresho — tooling / CI

## Kugerageza — Testing

- [ ] `./tests/run_tests.sh` (or `.\tests\run_tests.ps1`) passes
- [ ] Added a test case under `tests/cases/` for the behaviour this changes
- [ ] Expected output committed under `tests/expected/`

<!-- If this touches the code generator or include/x64asm.hpp: -->
- [ ] `./tools/enc/run_verify.sh` passes (encoder still matches GNU as byte-for-byte)

<!-- If this touches tools/runtime/runtime.s: -->
- [ ] Regenerated with `./tools/build_runtime.sh` and committed `include/runtime_blob.hpp`

<!-- If this touches lib/: -->
- [ ] Regenerated with `python3 tools/gen_stdlib_docs.py` and committed `docs/isomero.md`

## Ibindi — Anything reviewers should know

<!-- Tradeoffs you made, things you are unsure about, things you deliberately
     left out. Saying "I wasn't sure about X" is useful, not a weakness. -->
