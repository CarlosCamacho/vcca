#!/bin/sh
# Packages src/web and build/vcc.wasm for Android, in one of two forms built
# from the same source. ./build.sh runs both; run this directly to make one.
#
#   tools/build-android.sh apk    -> dist/VCCA.apk       GitHub releases, sideloading
#       Android 5.0 and later (API 21), targets API 34. Package com.vcce.vcc,
#       the one every earlier release used, so it installs over them.
#
#   tools/build-android.sh play   -> dist/VCCA-play.aab  Google Play upload
#       Android 7.0 and later (API 24), targets API 36 as Play requires for
#       new apps and updates from 2026-08-31. Package $VCCA_PLAY_ID.
#
# Both compile against android-23.jar from the Debian/Ubuntu SDK packages
# (Google's SDK download site is not needed); the few newer calls the Play
# build makes go through reflection in MainActivity, guarded by API level.
# The Play build also needs Google's bundletool, fetched once into build/tools/.
#
# The version name comes from VERSION. versionCode stays in the manifest: it
# is Android's install-order counter, not the version, and must only go up.
set -e
cd "$(dirname "$0")/.."
FLAVOR=${1:-apk}
SDK=/usr/lib/android-sdk
JAR=$SDK/platforms/android-23/android.jar
PLAY_ID=${VCCA_PLAY_ID:-com.cocoscene.vcca}
BUNDLETOOL_VER=1.18.3
BUNDLETOOL_SHA=a099cfa1543f55593bc2ed16a70a7c67fe54b1747bb7301f37fdfd6d91028e29
BUNDLETOOL=build/tools/bundletool-all-$BUNDLETOOL_VER.jar
VERSION=$(cat VERSION)
B=build/android

# The signing key is never in the repository. An update installs over the old
# app only if it is signed with the same key, so keep the keystore and its
# password somewhere safe. Without one, a new key is made (fine for your own
# builds, but they will not install over the official APK). The same key
# signs the Play bundle as its upload key.
KEYSTORE=${VCCA_KEYSTORE:-vcc-release.keystore}
: "${VCCA_KEYSTORE_PASS:?set VCCA_KEYSTORE_PASS to the keystore password}"
[ -f "$KEYSTORE" ] || keytool -genkeypair -keystore "$KEYSTORE" -storepass:env VCCA_KEYSTORE_PASS -keypass:env VCCA_KEYSTORE_PASS \
	-alias vcc -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=VCC Android port" >/dev/null 2>&1

case $FLAVOR in
	apk) MIN=21; TARGET=34 ;;
	play) MIN=24; TARGET=36 ;;
	*) echo "usage: $0 apk|play" >&2; exit 2 ;;
esac

[ -f build/vcc.wasm ] || { echo "build/vcc.wasm is missing: run ./build.sh" >&2; exit 1; }
rm -rf $B && mkdir -p $B/classes $B/assets/web dist
cp src/web/index.html src/web/app.js src/web/audio-worklet.js build/vcc.wasm $B/assets/web/
javac -nowarn -Xlint:-options -source 8 -target 8 -bootclasspath $JAR -classpath $JAR -d $B/classes $(find src/android/src -name '*.java')
$SDK/build-tools/debian/dx --dex --output=$B/classes.dex $B/classes
aapt2 compile --dir src/android/res -o $B/res.zip
# minSdk and targetSdk are not in AndroidManifest.xml: each flavor sets its own.
LINK="-I $JAR --manifest src/android/AndroidManifest.xml -A $B/assets -R $B/res.zip --auto-add-overlay
	--min-sdk-version $MIN --target-sdk-version $TARGET --version-name $VERSION --replace-version"

if [ $FLAVOR = apk ]; then
	aapt2 link $LINK -0 wasm -o $B/unaligned.apk
	# A fixed date, as aapt2 gives every other entry, so the same source
	# always makes the same bytes.
	TZ=UTC touch -t 198001010000 $B/classes.dex
	(cd $B && TZ=UTC zip -q -X unaligned.apk classes.dex)
	zipalign -f -p 4 $B/unaligned.apk $B/aligned.apk
	apksigner sign --ks "$KEYSTORE" --ks-pass env:VCCA_KEYSTORE_PASS --key-pass env:VCCA_KEYSTORE_PASS --out dist/VCCA.apk $B/aligned.apk
	rm -f dist/VCCA.apk.idsig
	exit 0
fi

# Play: an Android App Bundle. aapt2 writes the resources in protobuf form,
# rearranged into a "base" module (manifest/, dex/, res/, assets/,
# resources.pb), which bundletool turns into the .aab Play takes.
if [ ! -f $BUNDLETOOL ]; then
	mkdir -p build/tools
	curl -fsSL -o $BUNDLETOOL.part https://github.com/google/bundletool/releases/download/$BUNDLETOOL_VER/bundletool-all-$BUNDLETOOL_VER.jar
	mv $BUNDLETOOL.part $BUNDLETOOL
fi
echo "$BUNDLETOOL_SHA  $BUNDLETOOL" | sha256sum -c --quiet
aapt2 link $LINK --proto-format --rename-manifest-package $PLAY_ID -o $B/proto.zip
mkdir -p $B/base/manifest $B/base/dex
(cd $B/base && unzip -q ../proto.zip && mv AndroidManifest.xml manifest/ && cp ../classes.dex dex/ && zip -qr ../base.zip .)
cat > $B/BundleConfig.json <<'JSON'
{ "compression": { "uncompressedGlob": ["assets/**.wasm"] } }
JSON
rm -f dist/VCCA-play.aab
java -jar $BUNDLETOOL build-bundle --modules=$B/base.zip --config=$B/BundleConfig.json --output=$B/unsigned.aab
jarsigner -keystore "$KEYSTORE" -storepass:env VCCA_KEYSTORE_PASS -keypass:env VCCA_KEYSTORE_PASS \
	-sigalg SHA256withRSA -digestalg SHA-256 -signedjar dist/VCCA-play.aab $B/unsigned.aab vcc >/dev/null
