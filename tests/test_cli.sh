#!/usr/bin/env bash
# End-to-end test for the `wandaa` project and package tool.
#
# Covers the whole dependency flow: scaffold a project, add a local dependency,
# vendor it, check the lockfile hash against sha256sum (an independent
# implementation), build against the vendored module and the standard library,
# run the project's tests, and confirm tampering with a vendored package is
# detected.
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

WANDAA="${WANDAA:-$ROOT/wandaa}"
export WANDAAC="${WANDAAC:-$ROOT/wandaac}"
[ -x "$WANDAA" ]  || { echo "not built: $WANDAA (run ./build.sh)"; exit 1; }
[ -x "$WANDAAC" ] || { echo "not built: $WANDAAC (run ./build.sh)"; exit 1; }

if command -v wine >/dev/null && [ "$(uname -s)" != "MINGW"* ]; then
    export WANDAA_RUNNER="${WANDAA_RUNNER:-wine}"
    export WINEDEBUG=-all
fi

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
fail=0
check() { if [ "$1" = 0 ]; then echo "  ok: $2"; else echo "  FAIL: $2"; fail=$((fail+1)); fi; }

cd "$W"

# A local package to depend on.
mkdir -p ibipimo
cat > ibipimo/ibipimo.waa <<'WAA'
umurimo metero_mu_santimetero(m) { tanga m * 100; }
WAA

echo "== tangira =="
"$WANDAA" tangira urugero >/dev/null
check $? "project scaffolded"
[ -f urugero/wandaa.toml ] && [ -f urugero/src/mbere.waa ] && [ -f urugero/tests/mbere_test.waa ]
check $? "manifest, source and test created"

cd urugero

echo "== ongeraho + shakisha =="
"$WANDAA" ongeraho ibipimo ../ibipimo >/dev/null
check $? "dependency added to manifest"
"$WANDAA" shakisha >/dev/null
check $? "dependency vendored"
[ -f ibipapuro/ibipimo/ibipimo.waa ]
check $? "vendored file present"

echo "== lockfile hash vs sha256sum =="
LOCKED=$(awk -F'\t' '$1=="ibipimo" {print $4}' wandaa.lock)
EXPECTED=$( (printf 'ibipimo.waa' && cat ibipapuro/ibipimo/ibipimo.waa) | sha256sum | cut -d' ' -f1 )
[ -n "$LOCKED" ] && [ "$LOCKED" = "$EXPECTED" ]
check $? "lockfile sha256 matches an independent implementation"

"$WANDAA" genzura >/dev/null
check $? "genzura passes on an untampered tree"

echo "== build against dependency + stdlib =="
cat > src/mbere.waa <<'WAA'
injiza "ibipimo/ibipimo.waa";
injiza "imibare.waa";
andika(metero_mu_santimetero(5));
andika(nini(3, 9));
WAA
OUT=$("$WANDAA" koresha 2>&1)
echo "$OUT" | grep -q '^500$' && echo "$OUT" | grep -q '^9$'
check $? "vendored module and stdlib both resolve and run"

echo "== gerageza =="
"$WANDAA" gerageza >/dev/null
check $? "project test suite runs and passes"

echo "== tamper detection =="
echo "# byahinduwe" >> ibipapuro/ibipimo/ibipimo.waa
"$WANDAA" genzura >/dev/null 2>&1
[ $? -ne 0 ]
check $? "genzura rejects a modified vendored package"

echo
if [ "$fail" -eq 0 ]; then echo "CLI OK"; else echo "$fail CLI check(s) failed"; fi
[ "$fail" -eq 0 ]
