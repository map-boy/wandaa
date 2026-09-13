# Wandaa Governance

Wandaa is open source under the [MIT License](LICENSE). This document says who
decides what, and how someone new can end up being one of the deciders.

## Stewardship

Wandaa is stewarded by **VAF UBWENGE TECH**, which funds maintenance, hosts the
infrastructure, and provides commercial support (see [ENTERPRISE.md](ENTERPRISE.md)).

Stewardship means responsibility, not ownership of the direction. The licence
is MIT and the project is forkable by anyone at any time. That is deliberate: a
language meant to be the technical foundation for other people's work should
not be something they can be locked out of. If VAF UBWENGE TECH ever stops
maintaining Wandaa, the community can continue it without asking permission.

## Roles

**Contributors** — anyone who opens an issue or a pull request. No prior
commitment required.

**Committers** — contributors with merge rights to `main`. Granted by the
maintainers after a track record of good contributions: roughly five merged
non-trivial pull requests, and review comments that show an understanding of
why the code is the way it is. Committers may merge others' work but not their
own without a second approval.

**Maintainers** — committers who also set direction, cut releases, and decide
disputed design questions. Maintainers are appointed by existing maintainers.

The current roster lives in [MAINTAINERS.md](MAINTAINERS.md).

## How decisions are made

Most changes need one committer approval and green CI. That is the normal path
and should stay boring.

Changes that need broader agreement — a **Wandaa Design Proposal (WDP)**:

- new or renamed keywords
- changes to the syntax or grammar
- changes to data representation (how strings or arrays are laid out)
- anything that breaks existing `.waa` programs
- adding a dependency to the compiler build
- changes to the FFI or module resolution rules

A WDP is an issue using the [design proposal template](.github/ISSUE_TEMPLATE/design_proposal.md).
It stays open for **at least seven days** so people in other timezones can
respond. Maintainers then accept, reject, or ask for revision, and **write down
why**. A rejected proposal with a clear reason is a useful artifact; a silently
closed one is not.

Where maintainers disagree, the decision goes to a simple majority. A tie means
the proposal is not accepted — the status quo wins ties.

## Language design principles

These are the standards a WDP is judged against. They exist so that "no" has a
reason behind it.

1. **Kinyarwanda first.** Keywords and standard library names are Kinyarwanda.
   Not transliterated English — words that a Kinyarwanda speaker would actually
   use for the concept. English aliases are acceptable for operators that are
   already symbolic across all languages (`&&` alongside `na`).

2. **A beginner must be able to install it.** Anything that adds an install
   step for end users needs a very strong argument. Removing the MinGW
   dependency was worth a compiler backend rewrite for exactly this reason.

3. **Small enough to understand.** A motivated student should be able to read
   the whole compiler. Features that would make that impossible need to earn
   their place.

4. **Correctness is checkable, not asserted.** Machine-code encodings are
   diffed against a real assembler. Backend changes are diffed against the
   previous backend's output. "I read the manual carefully" is not a
   verification strategy.

5. **Don't break working programs.** If a change breaks existing `.waa` files,
   it needs a migration path and a major version bump.

## Releases

Wandaa uses semantic versioning.

- **Patch** — bug fixes, no language change.
- **Minor** — new keywords, builtins or library functions; existing programs
  keep working.
- **Major** — anything that breaks existing programs.

Every release must have all CI jobs green, including the encoder ground-truth
diff, the runtime blob freshness check, and the bare-PATH self-containment
check. A release is cut by a maintainer tagging `vX.Y.Z` and writing release
notes that say what changed for people writing Wandaa, not just what changed in
the compiler.

## Security

Report vulnerabilities per [SECURITY.md](SECURITY.md) — not as a public issue.

## Changing this document

Governance changes are themselves WDPs, with a fourteen-day comment period
instead of seven.
