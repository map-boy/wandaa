#!/usr/bin/env bash
# ===========================================================================
#  Ground truth for the WANDAA code generator.
#
#  compiler/codegen.waa is the code generator written in Wandaa. For every
#  program in tests/bootstrap/ it must produce an executable BYTE-IDENTICAL to
#  the one wandaac produces -- not merely one that behaves the same.
#
#  Those programs are deliberately inside the subset compiler/codegen.waa
#  covers: integers, strings, arithmetic, comparisons, `reka`, assignment,
#  `niba`, `mugihe`, `hagarika`, `komeza`, functions and `andika`. Anything
#  outside it is refused by name rather than mis-compiled.
# ===========================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

WANDAAC="${WANDAAC:-./bin/wandaac}"
WORK=".codegen_verify"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

[ -x "$WANDAAC" ] || { echo "compiler not found: $WANDAAC"; exit 1; }
command -v wine >/dev/null 2>&1 && RUN="wine" || RUN=""

echo "==> building compiler/codegen.waa with the C++ compiler"
"$WANDAAC" -q compiler/codegen.waa "$WORK/codegen.exe"

ok=0
bad=0
for f in tests/bootstrap/*.waa; do
    [ -s "$f" ] || continue
    rm -f "$WORK/cpp.exe" "$WORK/waa.exe"
    "$WANDAAC" -q "$f" "$WORK/cpp.exe"
    # codegen.waa reads include/runtime_blob.txt relative to the working
    # directory, so it runs from the repository root.
    $RUN "$WORK/codegen.exe" "$f" "$WORK/waa.exe" > "$WORK/out.txt" 2>&1 || true
    if [ ! -s "$WORK/waa.exe" ]; then
        bad=$((bad+1))
        echo "FAILED TO BUILD: $f"
        head -3 "$WORK/out.txt"
        continue
    fi
    if cmp -s "$WORK/cpp.exe" "$WORK/waa.exe"; then
        ok=$((ok+1))
    else
        bad=$((bad+1))
        echo "DIFFERS: $f (first difference at byte $(cmp -l "$WORK/cpp.exe" "$WORK/waa.exe" | head -1 | awk '{print $1}'))"
    fi
done

echo
if [ "$bad" -ne 0 ]; then
    echo "WANDAA CODEGEN MISMATCH: $bad of $((ok+bad)) programs differ"
    exit 1
fi
echo "WANDAA CODEGEN OK: $ok programs compile to byte-identical executables"
