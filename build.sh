#!/usr/bin/env bash
# Build the Wandaa compiler (Linux/macOS cross-build).
#
# The compiler runs anywhere a C++17 compiler does; the .exe files it emits are
# native Windows x86-64 binaries. Use Wine to run them on Linux.
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -Wextra -o wandaac src/lexer.cpp src/parser.cpp src/modules.cpp src/codegen.cpp src/main.cpp
g++ -std=c++17 -O2 -Wall -Wextra -o wandaa  src/cli.cpp
echo "built: ./wandaac (compiler) and ./wandaa (project tool)"
