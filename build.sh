#!/usr/bin/env bash
# Build the Wandaa compiler (Linux/macOS cross-build).
#
# The compiler runs anywhere a C++17 compiler does; the .exe files it emits are
# native Windows x86-64 binaries. Use Wine to run them on Linux.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin
g++ -std=c++17 -O2 -Wall -Wextra -o bin/wandaac src/lexer.cpp src/parser.cpp src/modules.cpp src/codegen.cpp src/main.cpp
g++ -std=c++17 -O2 -Wall -Wextra -o bin/wandaa  src/cli.cpp
echo "built: bin/wandaac (compiler) and bin/wandaa (project tool)"
