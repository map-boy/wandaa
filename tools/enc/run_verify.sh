#!/usr/bin/env bash
# Ground-truth encoder test: assemble the generated GAS source with the real
# GNU assembler, extract .text, and diff it against X64Asm's bytes.
# Any single-byte difference fails, and the offending instruction is named.
set -euo pipefail
cd "$(dirname "$0")/../.."

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
AS="${AS:-x86_64-w64-mingw32-as}"
OBJCOPY="${OBJCOPY:-x86_64-w64-mingw32-objcopy}"
command -v "$AS" >/dev/null || { echo "error: $AS not found"; exit 1; }

g++ -std=c++17 -O2 -Wall -Wextra -o "$T/verify_enc" tools/enc/verify_enc.cpp

"$T/verify_enc" --asm "$T/gt.s"
"$T/verify_enc" --bin "$T/mine.bin"
"$T/verify_enc" --map "$T/insn.map"

"$AS" -o "$T/gt.o" "$T/gt.s"
"$OBJCOPY" -O binary --only-section=.text "$T/gt.o" "$T/gt.bin"

# GNU as pads .text up to the section alignment with nops. That padding is not
# an instruction, so compare the instruction bytes and then assert the tail is
# padding only -- never silently ignore a size difference.
MINE=$(stat -c%s "$T/mine.bin")
GT=$(stat -c%s "$T/gt.bin")
if [ "$GT" -lt "$MINE" ]; then
    echo "ENCODER MISMATCH: GNU as produced FEWER bytes ($GT) than the encoder ($MINE)"
    exit 1
fi
head -c "$MINE" "$T/gt.bin" > "$T/gt.trunc"
tail -c "+$((MINE + 1))" "$T/gt.bin" > "$T/gt.tail"
if [ -s "$T/gt.tail" ]; then
    if od -An -tx1 -v "$T/gt.tail" | tr -s ' ' '\n' | grep -qvE '^(90|00)?$'; then
        echo "ENCODER MISMATCH: trailing bytes from GNU as are not alignment padding:"
        od -An -tx1 -v "$T/gt.tail" | head -3
        exit 1
    fi
fi

if cmp -s "$T/mine.bin" "$T/gt.trunc"; then
    echo "ENCODER OK: $MINE instruction bytes match GNU as byte-for-byte"
    echo "            ($(wc -l < "$T/insn.map") instructions; $(stat -c%s "$T/gt.tail") bytes of as alignment padding ignored)"
    exit 0
fi

echo "ENCODER MISMATCH"
echo "  encoder: $(stat -c%s "$T/mine.bin") bytes, GNU as: $(stat -c%s "$T/gt.trunc") bytes"
off=$(cmp "$T/mine.bin" "$T/gt.trunc" 2>&1 | grep -o '[0-9][0-9]*' | head -1 || true)
if [ -n "${off:-}" ]; then
    off=$((off - 1))
    echo "  first differing byte at offset $off"
    awk -v o="$off" -F'\t' '$1<=o { ins=$2; at=$1 } END { printf "  in instruction: %s  (starts at %d)\n", ins, at }' "$T/insn.map"
    echo "  encoder bytes: $(od -An -tx1 -j $off -N 16 "$T/mine.bin")"
    echo "  GNU as  bytes: $(od -An -tx1 -j $off -N 16 "$T/gt.trunc")"
fi
exit 1
