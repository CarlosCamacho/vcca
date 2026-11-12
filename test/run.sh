#!/bin/sh
# Runs the test suite against build/vcc.wasm and src/web (run ./build.sh first).
#
#   test/run.sh            every test below; exit 1 if any fails or cannot run
#   test/run.sh tape5      just the named ones
#   test/run.sh --serve    only serve the front end on :8765, for tools/*.mjs
#
# Needs Node with Playwright (npm i playwright). sdc and sdcui run on the
# repository's own test ROM; the rest need the copyrighted ROMs in roms/ (or
# $VCCA_ROMS): coco3.rom and disk11.rom. Logs go to build/test-logs/.
#
# Every test asserts what it checks and exits with the number of failures; a
# test fails here if it exits non-zero or prints a line starting FAIL.
set -e
cd "$(dirname "$0")/.."
ROMS=${VCCA_ROMS:-roms}
[ -f build/vcc.wasm ] || { echo "build/vcc.wasm is missing: run ./build.sh" >&2; exit 1; }

# The front end as the app serves it: src/web plus the core.
rm -rf build/web && mkdir -p build/web build/test-logs
cp src/web/* build/vcc.wasm build/web/
(cd build/web && exec python3 -m http.server 8765 --bind 127.0.0.1 >/dev/null 2>&1) &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT INT TERM
sleep 1
if [ "$1" = --serve ]; then echo "Serving build/web on http://127.0.0.1:8765/ (Ctrl+C to stop)"; wait $SERVER; exit 0; fi

# Tests that need no Tandy ROM, then those that do. tape4 uses the tape
# tape makes. tape2, snd2 and ui11 take arguments and are run by hand
# (test/README.md). emes needs the emesDOS09 repository's dist/ (SD image and
# ROM): $EMES_DIST, or ../emesdos09/dist beside this repository.
NOROM="sdc sdcui"
EMES=emes; [ -d "${EMES_DIST:-../emesdos09/dist}" ] || { EMES=""; echo "SKIP  emes (no emesDOS09 dist/; set EMES_DIST)"; }
TESTS=${*:-"$NOROM timer vbord $EMES ui12 tape tape3 tape4 tape5"}
failed=""; missing=""
for t in $TESTS; do
	case " $NOROM " in
		*" $t "*) ;;
		*) if [ ! -f "$ROMS/coco3.rom" ] || [ ! -f "$ROMS/disk11.rom" ]; then missing="$missing $t"; continue; fi ;;
	esac
	log=build/test-logs/$t.log
	if (cd test && timeout 900 node $t.mjs) > $log 2>&1 && ! grep -q "^FAIL" $log; then
		echo "PASS  $t"
	else
		echo "FAIL  $t  (see $log)"; failed="$failed $t"
	fi
done
[ -z "$missing" ] || echo "NOT RUN:$missing (no coco3.rom and disk11.rom in $ROMS; they are copyrighted and not in the repository)"
[ -z "$failed" ] || { echo "Failed:$failed"; exit 1; }
[ -z "$missing" ] || exit 1
echo "All passed."
