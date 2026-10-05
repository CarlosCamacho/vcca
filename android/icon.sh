#!/bin/sh
# Makes the launcher icons and the README logo from assets/icon/vcca-icon.png,
# which is the source and is only ever read. Needs ImageMagick.
#
# The artwork has a transparent background and black lettering, so every copy
# sits on white: on a dark home screen or in GitHub's dark mode the letters
# would otherwise vanish.
#   - mipmap-*/ic_launcher.png: a white rounded tile, for Android 7 and older
#     launchers, which show the icon as it is.
#   - mipmap-*/ic_launcher_foreground.png + mipmap-anydpi-v26/ic_launcher.xml:
#     an adaptive icon for Android 8+. Launchers cut it to a circle or squircle
#     inside the middle 72dp of a 108dp canvas; at 56dp the logo's corners
#     (the V and the A reach them) stay inside even a circle.
set -e
cd "$(dirname "$0")"
SRC=../assets/icon/vcca-icon.png
tile() {   # size, output
	s=$1; r=$((s * 18 / 100)); inner=$((s * 86 / 100))
	convert -size ${s}x${s} xc:none -fill white -draw "roundrectangle 0,0,$((s - 1)),$((s - 1)),$r,$r" \
		\( "$SRC" -resize ${inner}x${inner} \) -gravity center -composite -depth 8 -strip "$2"
}
for d in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
	dir=res/mipmap-${d%%:*}; s=${d##*:}; mkdir -p $dir
	tile $s $dir/ic_launcher.png
	fg=$((s * 108 / 48)); logo=$((s * 56 / 48))
	convert -size ${fg}x${fg} xc:none \( "$SRC" -resize ${logo}x${logo} \) -gravity center -composite -depth 8 -strip $dir/ic_launcher_foreground.png
done
mkdir -p res/mipmap-anydpi-v26
cat > res/mipmap-anydpi-v26/ic_launcher.xml <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
XML
tile 512 ../docs/vcca-logo.png
