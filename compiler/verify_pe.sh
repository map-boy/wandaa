#!/usr/bin/env bash
# ===========================================================================
#  Ground truth for the WANDAA PE writer.
#
#  compiler/pe.waa builds a PE64 executable in Wandaa. tools/pe/pe_cases.txt
#  is a set of cases read by BOTH tools/pe/pe_run.cpp (using the C++
#  PEWriter) and compiler/pe_check.waa (using compiler/pe.waa). The two must
#  produce identical images, byte for byte, headers and all.
# ===========================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

WANDAAC="${WANDAAC:-./bin/wandaac}"
WORK=".pe_verify"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

[ -x "$WANDAAC" ] || { echo "compiler not found: $WANDAAC"; exit 1; }
command -v wine >/dev/null 2>&1 && RUN="wine" || RUN=""

echo "==> building the cases with the C++ PEWriter"
g++ -std=c++17 -O2 -Wall -Wextra -o "$WORK/pe_run" tools/pe/pe_run.cpp
"$WORK/pe_run" tools/pe/pe_cases.txt > "$WORK/expect.hex"

echo "==> building compiler/pe_check.waa with the C++ compiler"
"$WANDAAC" -q compiler/pe_check.waa "$WORK/pe_check.exe"

echo "==> building the same cases in Wandaa"
$RUN "$WORK/pe_check.exe" tools/pe/pe_cases.txt 2>/dev/null > "$WORK/got.hex"

n=$(wc -l < "$WORK/expect.hex" | tr -d ' ')
if ! diff -q "$WORK/expect.hex" "$WORK/got.hex" >/dev/null; then
    echo "WANDAA PE WRITER MISMATCH"
    i=1
    while [ "$i" -le "$n" ]; do
        a=$(sed -n "${i}p" "$WORK/expect.hex")
        b=$(sed -n "${i}p" "$WORK/got.hex")
        if [ "$a" != "$b" ]; then
            echo "  case $i differs: ${#a} vs ${#b} hex digits"
            # first differing byte, which is usually enough to name the field
            j=0
            while [ "$j" -lt "${#a}" ]; do
                if [ "${a:$j:2}" != "${b:$j:2}" ]; then
                    echo "    first difference at byte $((j/2)): ${a:$j:2} vs ${b:$j:2}"
                    break
                fi
                j=$((j+2))
            done
        fi
        i=$((i+1))
    done
    exit 1
fi
echo
echo "WANDAA PE WRITER OK: $n images build identically in Wandaa and in C++"
