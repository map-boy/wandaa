#!/usr/bin/env bash
# ===========================================================================
#  Ground truth for the Wandaa parser.
#
#  compiler/parser.waa is stage 2b of the bootstrap. This compiles it with the
#  C++ compiler, then runs BOTH parsers over every .waa file in the repository
#  and requires their AST dumps to be byte-identical.
#
#  The same principle as tools/enc/run_verify.sh: correctness is checked
#  against a working implementation rather than asserted.
# ===========================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

WANDAAC="${WANDAAC:-./bin/wandaac}"
# Kept inside the repository so every path below stays relative: the lexer is
# a WINDOWS binary, and handing it an absolute Unix path would not survive
# being run natively rather than under wine.
WORK=".parser_verify"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

[ -x "$WANDAAC" ] || { echo "compiler not found: $WANDAAC"; exit 1; }
command -v wine >/dev/null 2>&1 && RUN="wine" || RUN=""

echo "==> building compiler/parser.waa with the C++ compiler"
"$WANDAAC" -q compiler/parser.waa "$WORK/parser.exe"

ok=0
bad=0
for f in $(find tests/cases examples lib compiler -name '*.waa' 2>/dev/null | sort); do
    [ -s "$f" ] || continue                      # skip empty placeholders
    # A file the C++ lexer itself rejects has no stream to compare against.
    if ! "$WANDAAC" --ast "$f" > "$WORK/cpp.txt" 2>/dev/null; then continue; fi
    $RUN "$WORK/parser.exe" "$f" > "$WORK/waa.txt" 2>/dev/null
    if diff -q "$WORK/cpp.txt" "$WORK/waa.txt" >/dev/null; then
        ok=$((ok+1))
    else
        bad=$((bad+1))
        echo "DIFFERS: $f"
        diff "$WORK/cpp.txt" "$WORK/waa.txt" | head -10
    fi
done

echo
if [ "$bad" -ne 0 ]; then
    echo "PARSER MISMATCH: $bad of $((ok+bad)) files differ"
    exit 1
fi
echo "PARSER OK: $ok files parse to an identical tree in Wandaa and in C++"
