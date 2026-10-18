#!/bin/sh
# Runs the test suite against build/vcc.wasm and src/web (run ./build.sh first).
#
#   test/run.sh            every test below; exit 1 if any fails
#   test/run.sh tape5      just the named ones
#   test/run.sh --serve    only serve the front end on :8765, for tools/*.mjs
#
# Needs Node with Playwright (npm i playwright) and the copyrighted ROMs in
# roms/ (or $VCCA_ROMS): coco3.rom and disk11.rom. Logs go to build/test-logs/.
#
# A test fails if it exits non-zero, prints FAIL, or (for the ones that drive
# the page) reports a page error instead of "no errors". Several tests also
# print what they saw for a person to read; see test/README.md for which.
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

for f in coco3.rom disk11.rom; do
	[ -f "$ROMS/$f" ] || { echo "test/run.sh: $ROMS/$f is missing (ROMs are copyrighted and not in the repository)" >&2; exit 1; }
done

# Pass/fail tests first; the page-driving ones after. tape2, snd2 and ui11 take
# arguments and are run by hand (test/README.md).
TESTS=${*:-"timer vbord ui12 tape tape3 tape4 tape5"}
failed=""
for t in $TESTS; do
	log=build/test-logs/$t.log
	if (cd test && timeout 900 node $t.mjs) > $log 2>&1 && ! grep -q "FAIL" $log \
		&& { [ $t = timer ] || [ $t = vbord ] || grep -q "^no errors$" $log; }; then
		echo "PASS  $t"
	else
		echo "FAIL  $t  (see $log)"; failed="$failed $t"
	fi
done
[ -z "$failed" ] || { echo "Failed:$failed"; exit 1; }
echo "All passed."
