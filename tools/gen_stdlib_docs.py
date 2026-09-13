#!/usr/bin/env python3
"""Regenerate docs/isomero.md from the .waa sources in lib/.

The standard library reference is generated rather than hand-written so it
cannot drift from the code. Run from the repository root:

    python3 tools/gen_stdlib_docs.py

CI regenerates it and fails if the result differs from the committed copy.
"""
import os
import re
import sys

MODULES = [
    ("imibare.waa",  "Imibare — Arithmetic"),
    ("amagambo.waa", "Amagambo — Strings"),
    ("urutonde.waa", "Urutonde — Lists"),
    ("sisitemu.waa", "Sisitemu — Windows system calls"),
]

HEADER = """# Isomero rusange — Standard Library Reference

*Iyi nyandiko ikurwa mu masomero ubwayo. This page is generated from the
sources in `lib/` by `tools/gen_stdlib_docs.py`; edit the `.waa` files, not
this file.*

Kwinjiza module — to use a module:

```wandaa
injiza "imibare.waa";
```

"""


def render(root):
    out = [HEADER]
    for filename, title in MODULES:
        path = os.path.join(root, "lib", filename)
        with open(path, encoding="utf-8") as handle:
            lines = handle.read().split("\n")

        out.append("---\n\n## `%s` — %s\n" % (filename, title))
        pending = []
        for line in lines:
            stripped = line.strip()

            # A blank line ends a doc block, so file headers and unrelated
            # commentary do not get attached to the next definition.
            if not stripped:
                pending = []
                continue

            # Comment lines immediately above a definition become its doc text.
            if stripped.startswith("#"):
                body = stripped.lstrip("#").strip()
                if body and not set(body) <= set("= "):
                    pending.append(body)
                continue

            match = re.match(r"^umurimo\s+(\w+)\s*\(([^)]*)\)", stripped)
            if match:
                name, params = match.group(1), match.group(2).strip()
                doc = " ".join(pending) if pending else "_(nta gisobanuro)_"
                out.append("**`%s(%s)`**  \n%s\n" % (name, params, doc))
                pending = []
                continue

            match = re.match(r'^hanze\s+"([^"]+)"\s+(\w+)\s*\(([^)]*)\)', stripped)
            if match:
                dll, name, params = match.groups()
                out.append("**`%s(%s)`** — umurimo wo hanze muri `%s` "
                           "(external, from `%s`)\n" % (name, params.strip(), dll, dll))
                pending = []
                continue

            # Any other code resets the pending doc comment.
            pending = []
    return "\n".join(out) + "\n"


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    text = render(root)
    target = os.path.join(root, "docs", "isomero.md")
    with open(target, "w", encoding="utf-8") as handle:
        handle.write(text)
    print("wrote %s" % target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
