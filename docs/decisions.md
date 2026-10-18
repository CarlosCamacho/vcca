# Decisions

One entry per significant decision: context, choice, consequence. A reversed
decision is marked *superseded*, never deleted.

## 1. VCC's core in WebAssembly, in a WebView (2026-10-04)

**Context:** VCC is C++ written for Win32. A native Android port would mean an
NDK build plus a native UI. **Choice:** compile the emulation core to
`wasm32-wasi` with a few Windows shims (`src/compat`) and write the UI in
HTML/JS, in a WebView. **Consequence:** one front end runs in a desktop
browser too, so `test/run.sh` exercises the real UI in Chromium; save states
are a copy of WebAssembly memory. The app needs a WebView of Chrome 80+.

## 2. GPL-3.0-or-later (2026-10-04)

VCC is GPL v3; a port of it must be too. MAME cartridge drivers ported here are
BSD-3-Clause, compatible, with their notices kept in the sources.

## 3. Bug reports welcome, code contributions not (2026-10-05)

The owner wants people to download and report bugs; only the owner (and
Claude, working for the owner) push code. GitHub: issue forms
(`.github/ISSUE_TEMPLATE`), `CONTRIBUTING.md` saying pull requests are not
accepted, a ruleset blocking force-pushes and deletion of `main`, private
vulnerability reporting. Commits are authored by the owner, with no
co-author trailers.

## 4. VCCA's own version numbers (2026-10-05)

**Context:** the first release was "2.1.9-android10", which read as "for
Android 10" — and the VCC source was actually 2.1.10.0. **Choice:** VCCA
numbers itself from 1.1.0 (the first release counts as 1.0); About shows the
VCC version separately. **Consequence:** `VERSION` is VCCA's version only.

## 5. Emulation fixes against upstream VCC, each with a test ROM (2026-10-07)

**Context:** the GIME timer drifted in scanline mode and VBORD fired 21 lines
early, both measured from a CoCo 3 game's raster effects. **Choice:** fix them
in `src/core` to match XRoar's source and the hardware references, each with a
test ROM that fails before the fix. 1986/1987 GIME is a setting, default 1987
(VCC's behavior). **Consequence:** VCCA's timing deliberately differs from
upstream VCC; save states break whenever the core changes. Neither fix has
been checked on real hardware.

## 6. Two Android builds (2026-10-10)

**Context:** Google Play requires API 36 and an App Bundle; the GitHub APK
must keep installing over earlier releases and reach older phones.
**Choice:** one source, two packages: `VCCA.apk` (`com.vcce.vcc`, API 21+,
target 34) and `VCCA-play.aab` (`com.cocoscene.vcca`, API 24+, target 36).
Built with the Ubuntu SDK tools and bundletool rather than Gradle, because
Google's SDK download site is unreachable from the build environment; newer
APIs are called by reflection. **Consequence:** the two can be installed side
by side and do not share data. The Play package name is permanent once
uploaded. Both signed with the same key, which Play App Signing should be
given as the existing app signing key.

## 7. Credit as CoCoScene (2026-10-10)

The owner's name is not used for VCCA's credits, privacy policy or Play
package; CoCoScene (the owner's domain, cocoscene.com) is. The GitHub
repository is still under the owner's account; moving it to an organization
would change the links.

## 8. Repository layout (2026-10-18)

Reorganized to the CoCo project standard (`coco-tooling →
coco-project-organization`): `src/ tools/ test/ assets/ docs/ dist/`, one
`./build.sh`, `CHANGELOG.md` with `### Learned`, `CLAUDE.md`. Two departures
for an Android emulator rather than a CoCo game: `dist/` holds `VCCA.apk`
(not a `.dsk`), and there is no `lib/coco-common` (no 6809 code is shared
with other projects). `PRIVACY.md` stays at the root because Google Play
links to it there.
