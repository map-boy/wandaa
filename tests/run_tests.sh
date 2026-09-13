#!/usr/bin/env bash
# Wandaa end-to-end test suite (Linux, via Wine).
#
# Compiles every example and test case with wandaac -- no assembler, no linker
# -- runs the resulting native Windows .exe under Wine, and asserts stdout and
# exit code match the frozen expectations in tests/expected/.
set -uo pipefail
cd "$(dirname "$0")/.."

WANDAAC="${WANDAAC:-./wandaac}"
[ -x "$WANDAAC" ] || { echo "compiler not found: $WANDAAC"; exit 1; }
command -v wine >/dev/null || { echo "wine not found"; exit 1; }

export WINEDEBUG=-all
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
pass=0; fail=0

for src in tests/cases/*.waa examples/*.waa; do
    name="$(basename "$src" .waa)"
    if ! "$WANDAAC" "$src" "$WORK/$name.exe" >/dev/null 2>&1; then
        echo "FAIL  $name (compile)"; fail=$((fail+1)); continue
    fi
    # Run from the work directory: some cases write a file next to themselves.
    ( cd "$WORK" && timeout 120 wine "./$name.exe" >"$WORK/$name.actual" 2>/dev/null; echo $? >"$WORK/$name.code" )
    actual_exit="$(cat "$WORK/$name.code")"
    expected_exit="$(cat "tests/expected/$name.exit" 2>/dev/null || echo 0)"

    if diff -q "tests/expected/$name.out" "$WORK/$name.actual" >/dev/null 2>&1 \
       && [ "$actual_exit" = "$expected_exit" ]; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name (exit $actual_exit, expected $expected_exit)"
        diff "tests/expected/$name.out" "$WORK/$name.actual" | head -10
        fail=$((fail+1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
