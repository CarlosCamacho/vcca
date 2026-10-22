#!/bin/sh
# One command from a clean clone: builds vcc.wasm, then both Android packages,
# and checks what it made.
#
#   ./build.sh                                -> build/vcc.wasm only
#   VCCA_KEYSTORE_PASS=… ./build.sh           -> also dist/VCCA.apk and dist/VCCA-play.aab
#   REBUILD=1 ./build.sh                      -> recompile every source file
#
# Signing needs the release keystore (never in the repository): ./vcc-release.keystore
# or $VCCA_KEYSTORE. Without VCCA_KEYSTORE_PASS the Android step is skipped, so the
# core and the front end can still be built and tested by anyone.
set -e
cd "$(dirname "$0")"

# Toolchain, pinned. A different clang may well work, but nothing has been
# tested with one, and a quiet difference in the core is the worst kind.
CLANG_MAJOR=18
clang --version | head -1 | grep -q "clang version $CLANG_MAJOR\." || {
	echo "build.sh: needs clang $CLANG_MAJOR (found: $(clang --version | head -1)); see README, Building" >&2; exit 1; }

# The version lives in VERSION. app.js shows it in About and in bug reports,
# so it must agree; checked here rather than generated, so src/web still runs
# straight from the folder.
VERSION=$(cat VERSION)
grep -q "^const VERSION = '$VERSION';" src/web/app.js || {
	echo "build.sh: src/web/app.js VERSION does not match VERSION ($VERSION)" >&2; exit 1; }

mkdir -p build/obj
CXX="clang++ --target=wasm32-wasi --sysroot=/usr"
FLAGS="-std=c++17 -O2 -fno-exceptions -fno-rtti -DNDEBUG -Isrc/core -Isrc/compat -Wno-everything"
SRCS="core/mc6809.cpp core/hd6309.cpp core/tcc1014graphics.cpp core/tcc1014mmu.cpp
core/tcc1014registers.cpp core/mc6821.cpp core/iobus.cpp core/coco3.cpp core/keyboard.cpp
core/keyboardLayout.cpp core/joystickinput.cpp core/fd502/wd1793.cpp core/fd502/distortc.cpp
core/ssc/tms7000.cpp core/ssc/ay8913.cpp core/ssc/sp0256.cpp core/ssc/ssc_core.cpp core/gmc/sn76496.cpp
port/vfs.cpp port/pak.cpp port/tape.cpp port/host.cpp"
OBJS=""
for s in $SRCS; do
	o=build/obj/$(echo $s | tr / _).o
	if [ ! -f $o ] || [ src/$s -nt $o ] || [ -n "$REBUILD" ]; then
		$CXX $FLAGS -c src/$s -o $o &
	fi
	OBJS="$OBJS $o"
done
wait
clang --target=wasm32-wasi --sysroot=/usr -O2 -c src/port/cxa.c -o build/obj/cxa.o
$CXX -O2 -mexec-model=reactor $OBJS build/obj/cxa.o -o build/vcc.wasm -Wl,--strip-debug -Wl,-z,stack-size=1048576
echo "build/vcc.wasm  $(wc -c < build/vcc.wasm) bytes"

if [ -z "$VCCA_KEYSTORE_PASS" ]; then
	echo "Android packages skipped: set VCCA_KEYSTORE_PASS to build dist/VCCA.apk and dist/VCCA-play.aab."
	exit 0
fi
tools/build-android.sh apk
tools/build-android.sh play

# Verify what was made, not what was meant: signatures, the signing
# certificate (an APK signed with any other key will not update installed
# copies), the version and Android levels each package declares, and the
# bundle's structure as Google's own tool sees it.
RELEASE_CERT=a094a684708b3c3cb6c7a26f14e51ca12aa3854e1818f67929fb8d9d6f0b5172
apksigner verify dist/VCCA.apk
cert=$(apksigner verify --print-certs dist/VCCA.apk | sed -n 's/.*SHA-256 digest: //p')
[ "$cert" = $RELEASE_CERT ] || echo "WARNING: dist/VCCA.apk is not signed with the release key ($cert); it will not update installed copies." >&2
badging=$(aapt dump badging dist/VCCA.apk)
echo "$badging" | grep -q "name='com.vcce.vcc'.*versionName='$VERSION'" || { echo "build.sh: VCCA.apk package/version wrong" >&2; exit 1; }
echo "$badging" | grep -q "^sdkVersion:'21'" && echo "$badging" | grep -q "^targetSdkVersion:'34'" || { echo "build.sh: VCCA.apk SDK levels wrong" >&2; exit 1; }
BT="java -jar build/tools/bundletool-all-1.18.3.jar"
jarsigner -verify dist/VCCA-play.aab 2>/dev/null | grep -q "^jar verified" || { echo "build.sh: VCCA-play.aab signature does not verify" >&2; exit 1; }
$BT validate --bundle=dist/VCCA-play.aab >/dev/null
manifest=$($BT dump manifest --bundle=dist/VCCA-play.aab)
echo "$manifest" | grep -q "android:versionName=\"$VERSION\".*package=\"${VCCA_PLAY_ID:-com.cocoscene.vcca}\"" || { echo "build.sh: VCCA-play.aab package/version wrong" >&2; exit 1; }
echo "$manifest" | grep -q 'android:minSdkVersion="24" android:targetSdkVersion="36"' || { echo "build.sh: VCCA-play.aab SDK levels wrong" >&2; exit 1; }
(cd dist && sha256sum VCCA.apk > SHA256SUMS)
echo "dist/VCCA.apk       $(wc -c < dist/VCCA.apk) bytes  com.vcce.vcc $VERSION  Android 5.0+  verified"
echo "dist/VCCA-play.aab  $(wc -c < dist/VCCA-play.aab) bytes  ${VCCA_PLAY_ID:-com.cocoscene.vcca} $VERSION  Android 7.0+  verified"
