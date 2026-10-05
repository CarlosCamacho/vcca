#!/bin/sh
# Packages the web front end and vcc.wasm into a signed APK using the
# Debian/Ubuntu Android SDK packages (aapt, dx, zipalign, apksigner).
set -e
cd "$(dirname "$0")"
SDK=/usr/lib/android-sdk
JAR=$SDK/platforms/android-23/android.jar
rm -rf build && mkdir -p build/classes build/apk assets/web
cp ../web/index.html ../web/app.js ../web/audio-worklet.js ../out/vcc.wasm assets/web/
javac -nowarn -Xlint:-options -source 8 -target 8 -bootclasspath $JAR -classpath $JAR -d build/classes $(find src -name '*.java')
$SDK/build-tools/debian/dx --dex --output=build/apk/classes.dex build/classes
aapt package -f -M AndroidManifest.xml -S res -A assets -I $JAR -F build/unaligned.apk -0 wasm
(cd build/apk && aapt add ../unaligned.apk classes.dex >/dev/null)
zipalign -f -p 4 build/unaligned.apk build/aligned.apk
# The signing key is never in the repository. An update installs over the old
# app only if it is signed with the same key, so keep the keystore and its
# password somewhere safe. Without one, a new key is made (fine for your own
# builds, but they will not install over the official APK).
KEYSTORE=${VCCA_KEYSTORE:-vcc-release.keystore}
: "${VCCA_KEYSTORE_PASS:?set VCCA_KEYSTORE_PASS to the keystore password}"
[ -f "$KEYSTORE" ] || keytool -genkeypair -keystore "$KEYSTORE" -storepass:env VCCA_KEYSTORE_PASS -keypass:env VCCA_KEYSTORE_PASS \
	-alias vcc -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=VCC Android port" >/dev/null 2>&1
apksigner sign --ks "$KEYSTORE" --ks-pass env:VCCA_KEYSTORE_PASS --key-pass env:VCCA_KEYSTORE_PASS --out ../out/VCCA.apk build/aligned.apk
apksigner verify --print-certs ../out/VCCA.apk | head -2
ls -la ../out/VCCA.apk
