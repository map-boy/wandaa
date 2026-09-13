# Wandaa for Enterprise — VAF UBWENGE TECH

Wandaa is free and MIT-licensed. It always will be. This page is for
organisations that need more than a licence: support commitments, training, and
someone to call.

**Wandaa is stewarded by VAF UBWENGE TECH**, which funds its maintenance and
provides the commercial services below.

---

## Why an organisation would use Wandaa

**Your developers work in their own language.** Onboarding a junior developer
in Kigali or Musanze does not also mean teaching them English keywords first.
The cognitive load of the language itself drops out of the picture.

**One file to deploy.** `wandaac.exe` compiles straight to a native Windows
executable — no VM, no interpreter, no runtime to install on target machines.
The output is a standalone `.exe`, which matters for field deployments on
machines with limited connectivity.

**It can reach your existing systems.** Wandaa's FFI calls any Windows DLL
directly, so existing C, C++ and COM components remain usable. You do not have
to rewrite what already works.

---

## Being straight about maturity

Wandaa is **pre-1.0**. An honest assessment matters more than a sales pitch,
so here is where it actually stands.

**Ready now:**

- Integer and string computation, arrays, functions, recursion, control flow
- Native Windows x86-64 executables with no external toolchain
- FFI to any Windows DLL
- A module system and a small standard library
- Runtime fault reporting with source line numbers
- A test suite and CI that verifies machine-code encodings against a real
  assembler

**Not ready yet** — see [ROADMAP.md](ROADMAP.md) for the plan and status:

- No floating point. Anything involving money-with-cents, statistics, or ML
  numerics is blocked on this.
- No memory reclamation. Long-running processes that build strings in a loop
  grow until they exit. Suitable for batch jobs and tools; not yet for daemons.
- No bounds checking on array indexing.
- Database, web and ML libraries are designed but not built.
- Windows only.

If your use case depends on something in the second list, talk to us about
timelines before committing, not after.

---

## Commercial services

**Support subscriptions** — guaranteed response times, a named contact, and
priority triage for bugs that block you. Includes advance notice of breaking
changes.

**Long-term support builds** — a pinned compiler version with backported
security and correctness fixes, so a deployed fleet does not have to track
`main`.

**Training** — Kinyarwanda-first training for development teams, from
introductory programming through compiler internals. Delivered in Kigali or
remotely.

**Custom development** — prioritised roadmap work, private FFI bindings to your
in-house systems, and integration work.

**Migration and assessment** — an evaluation of whether Wandaa fits a given
workload, including an honest answer when it does not.

Contact: **enterprise@vafubwengetech.rw**

---

## Our commitment to the open source project

These commitments exist so that depending on Wandaa is not a bet on one
company's goodwill:

1. **The compiler stays MIT-licensed.** No open-core split, no features held
   back for paying customers. The compiler a paying customer runs is the
   compiler in this repository.

2. **Development happens in public.** Roadmap, design proposals and decisions
   are in the open repository, not in a private tracker.

3. **Governance is not owned by us.** [GOVERNANCE.md](GOVERNANCE.md) describes
   how committers and maintainers are appointed from the community.

4. **The project is forkable, and we mean it.** If VAF UBWENGE TECH ever stops
   maintaining Wandaa, everything needed to continue it — source, build
   tooling, the runtime blob generator, the test suite — is in this repository
   under a permissive licence.

---

## Attribution

"VAF UBWENGE TECH" and "Wandaa" name the project's steward and the language.
Use of the Wandaa name for the language, for compatible implementations, and
for teaching material is welcome and unrestricted. Please do not imply
endorsement by VAF UBWENGE TECH for a product or service without asking.
