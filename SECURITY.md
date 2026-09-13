# Security Policy

## Supported versions

Wandaa is pre-1.0. Security fixes land on `main` and in the next release. There
is no long-term support branch yet.

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Report privately through GitHub's
[private vulnerability reporting](https://github.com/map-boy/wandaa/security/advisories/new),
or email **security@vafubwengetech.rw**.

Please include:

- what the problem is and what an attacker can do with it
- a minimal `.waa` file or compiler input that demonstrates it
- the Wandaa version (`git rev-parse HEAD` if built from source) and Windows version

We aim to acknowledge within **3 working days** and to give an assessment and
timeline within **10 working days**. If you do not hear back, please chase —
silence is a failure on our side, not a decision.

We will credit you in the advisory unless you ask us not to.

## What counts as a vulnerability

**In scope:**

- The compiler crashing, hanging, or writing outside its output path on
  malformed `.waa` input.
- Generated executables containing memory-safety bugs that a Wandaa program's
  own *data* can trigger — for example, a string operation that overruns its
  allocation given an attacker-controlled input string.
- Anything that causes `wandaac` to execute code from a source file at compile
  time, or to read or write files the user did not ask it to.
- Module resolution reaching outside the intended search paths.

**Out of scope, and important to be honest about:**

- **Wandaa is not a sandbox.** A `.waa` file can declare any DLL function with
  `hanze` and call it. Compiling and running an untrusted `.waa` file is
  equivalent to running an untrusted executable. That is by design, and it is
  not a vulnerability.
- **Array indexing is not bounds-checked.** `a[999]` on a three-element array
  reads out of bounds. This is a known limitation on the roadmap, not a
  security issue in itself — though a *specific* exploit path in the standard
  library or runtime is worth reporting.
- **There is no deallocation.** Long-running programs that build strings in a
  loop exhaust memory. Known, on the roadmap.
- Findings from automated scanners without a demonstrated impact.

## Security of the build

`include/runtime_blob.hpp` is generated machine code checked into the
repository. Because pre-built bytes are an obvious place to hide something, CI
regenerates the blob from `tools/runtime/runtime.s` on every push and fails if
it differs from the committed copy. You can run the same check yourself:

```bash
./tools/build_runtime.sh && git diff --exit-code include/runtime_blob.hpp
```

Review `runtime.s`, not the byte array.
