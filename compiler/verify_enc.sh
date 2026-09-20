#!/usr/bin/env bash
# ===========================================================================
#  Ground truth for the WANDAA instruction encoder.
#
#  compiler/x64.waa is the x86-64 encoder written in Wandaa, part of stage 2c
#  of the bootstrap. tools/enc/enc_script.cpp writes a case script and the
#  bytes X64Asm emits for each case; compiler/enc_check.waa encodes the same
#  cases with the Wandaa encoder. The two must agree byte-for-byte.
#
#  This chains onto tools/enc/run_verify.sh, which checks X64Asm itself
#  against GNU as -- so the Wandaa encoder is verified transitively against a
#  real assembler rather than against the manual.
# ===========================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

WANDAAC="${WANDAAC:-./bin/wandaac}"
WORK=".enc_verify"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

[ -x "$WANDAAC" ] || { echo "compiler not found: $WANDAAC"; exit 1; }
command -v wine >/dev/null 2>&1 && RUN="wine" || RUN=""

echo "==> generating the shared cases from the C++ encoder"
g++ -std=c++17 -O2 -Wall -Wextra -o "$WORK/enc_script" tools/enc/enc_script.cpp
"$WORK/enc_script" "$WORK/script.txt" "$WORK/expect.hex"

echo "==> building compiler/enc_check.waa with the C++ compiler"
"$WANDAAC" -q compiler/enc_check.waa "$WORK/enc_check.exe"

echo "==> encoding the same cases in Wandaa"
$RUN "$WORK/enc_check.exe" "$WORK/script.txt" 2>/dev/null > "$WORK/got.hex"

if ! diff -q "$WORK/expect.hex" "$WORK/got.hex" >/dev/null; then
    echo "WANDAA ENCODER MISMATCH"
    paste -d' ' "$WORK/script.txt" "$WORK/expect.hex" "$WORK/got.hex" |
        awk '{ if ($(NF-1) != $NF) print }' | head -20
    exit 1
fi
echo
echo "WANDAA ENCODER OK: $(wc -l < "$WORK/expect.hex" | tr -d ' ') cases encode identically in Wandaa and in C++"
