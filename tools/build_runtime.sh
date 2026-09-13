#!/usr/bin/env bash
# Regenerate include/runtime_blob.hpp from tools/runtime/runtime.s.
#
# BUILD-TIME ONLY. This is the one place an assembler is used, and it runs on
# the Wandaa compiler's own build machine -- never on an end user's. Users only
# ever run wandaac.exe.
#
# Requires: x86_64-w64-mingw32-as (binutils-mingw-w64-x86-64), g++.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

AS="${AS:-x86_64-w64-mingw32-as}"
command -v "$AS" >/dev/null || { echo "error: $AS not found (install binutils-mingw-w64-x86-64)"; exit 1; }

echo "==> assembling tools/runtime/runtime.s"
"$AS" -o "$OUT/runtime.o" tools/runtime/runtime.s

echo "==> building extractor"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/extract_blob" tools/extract_blob.cpp

echo "==> extracting blob"
"$OUT/extract_blob" "$OUT/runtime.o" include/runtime_blob.hpp

echo "==> ok: include/runtime_blob.hpp regenerated"
