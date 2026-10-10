#!/bin/sh
# Packages the web front end and vcc.wasm for Android, in one of two forms
# built from the same source:
#
#   android/build.sh apk    -> out/VCCA.apk       GitHub releases, sideloading
#       Android 5.0 and later (API 21), targets API 34. Package com.vcce.vcc,
#       the one every earlier release used, so it installs over them.
#
#   android/build.sh play   -> out/VCCA-play.aab  Google Play upload
#       Android 7.0 and later (API 24), targets API 36 as Play requires for
#       new apps and updates from 2026-08-31. Package $VCCA_PLAY_ID.
#
# Both compile against android-23.jar from the Debian/Ubuntu SDK packages
# (Google's SDK download site is not needed); the few newer calls the Play
# build makes go through reflection in MainActivity, guarded by API level.
# The Play build also needs Google's bundletool, fetched once into tools/.
set -e
cd "$(dirname "$0")"
FLAVOR=${1:-apk}
SDK=/usr/lib/android-sdk
JAR=$SDK/platforms/android-23/android.jar
PLAY_ID=${VCCA_PLAY_ID:-com.carloscamacho.vcca}
BUNDLETOOL_VER=1.18.3
BUNDLETOOL_SHA=a099cfa1543f55593bc2ed16a70a7c67fe54b1747bb7301f37fdfd6d91028e29
BUNDLETOOL=tools/bundletool-all-$BUNDLETOOL_VER.jar

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

rm -rf build && mkdir -p build/classes assets/web
cp ../web/index.html ../web/app.js ../web/audio-worklet.js ../out/vcc.wasm assets/web/
javac -nowarn -Xlint:-options -source 8 -target 8 -bootclasspath $JAR -classpath $JAR -d build/classes $(find src -name '*.java')
$SDK/build-tools/debian/dx --dex --output=build/classes.dex build/classes
aapt2 compile --dir res -o build/res.zip
# minSdk and targetSdk are not in AndroidManifest.xml: each flavor sets its own.
LINK="-I $JAR --manifest AndroidManifest.xml -A assets -R build/res.zip --auto-add-overlay --min-sdk-version $MIN --target-sdk-version $TARGET"

if [ $FLAVOR = apk ]; then
	aapt2 link $LINK -0 wasm -o build/unaligned.apk
	(cd build && zip -q unaligned.apk classes.dex)
	zipalign -f -p 4 build/unaligned.apk build/aligned.apk
	apksigner sign --ks "$KEYSTORE" --ks-pass env:VCCA_KEYSTORE_PASS --key-pass env:VCCA_KEYSTORE_PASS --out ../out/VCCA.apk build/aligned.apk
	apksigner verify --print-certs ../out/VCCA.apk | head -2
	ls -la ../out/VCCA.apk
	exit 0
fi

# Play: an Android App Bundle. aapt2 writes the resources in protobuf form,
# rearranged into a "base" module (manifest/, dex/, res/, assets/,
# resources.pb), which bundletool turns into the .aab Play takes.
if [ ! -f $BUNDLETOOL ]; then
	mkdir -p tools
	curl -fsSL -o $BUNDLETOOL.part https://github.com/google/bundletool/releases/download/$BUNDLETOOL_VER/bundletool-all-$BUNDLETOOL_VER.jar
	mv $BUNDLETOOL.part $BUNDLETOOL
fi
echo "$BUNDLETOOL_SHA  $BUNDLETOOL" | sha256sum -c --quiet
aapt2 link $LINK --proto-format --rename-manifest-package $PLAY_ID -o build/proto.zip
mkdir -p build/base/manifest build/base/dex
(cd build/base && unzip -q ../proto.zip && mv AndroidManifest.xml manifest/ && cp ../classes.dex dex/ && zip -qr ../base.zip .)
cat > build/BundleConfig.json <<'JSON'
{ "compression": { "uncompressedGlob": ["assets/**.wasm"] } }
JSON
rm -f ../out/VCCA-play.aab
java -jar $BUNDLETOOL build-bundle --modules=build/base.zip --config=build/BundleConfig.json --output=build/unsigned.aab
jarsigner -keystore "$KEYSTORE" -storepass:env VCCA_KEYSTORE_PASS -keypass:env VCCA_KEYSTORE_PASS \
	-sigalg SHA256withRSA -digestalg SHA-256 -signedjar ../out/VCCA-play.aab build/unsigned.aab vcc >/dev/null
jarsigner -verify ../out/VCCA-play.aab | head -1
ls -la ../out/VCCA-play.aab
