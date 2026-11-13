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

The owner wants people to download and report bugs; only the owner pushes
code. GitHub: issue forms
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
`./build.sh`, `CHANGELOG.md` with `### Learned`, `AGENTS.md` (was `CLAUDE.md`). Two departures
for an Android emulator rather than a CoCo game: `dist/` holds `VCCA.apk`
(not a `.dsk`), and there is no `lib/coco-common` (no 6809 code is shared
with other projects). `PRIVACY.md` stays at the root because Google Play
links to it there.

## 9. The CoCoSDC: VCC's command mode, the WD1793 model for floppy mode (2026-11-12)

**Context:** VCC 2.1.10 has a CoCoSDC simulator (`sdc/sdc.cpp`) with command
mode and its own floppy emulation. Its floppy emulation writes nothing, never
sets BUSY, and holds DRQ until a 257th read; the hardware skill records each of
these breaking a real project. How the real card's firmware emulates the
WD1773 is not documented anywhere readable here.
**Choice:** port VCC's command mode, but send the floppy personality to the
FD-502's WD1793 model with the card's images as drives 0 and 1, so Disk BASIC
1.1 can read and write through it as through the chip it imitates. Hold BUSY
briefly after each command (VCC answers at once) so a program that does not
wait for the card fails in VCCA too. **Consequence:** VCCA's floppy-mode
behaviour is the WD1793 model's, not VCC's and not necessarily the card's:
a program tested only through the floppy emulation still needs a real card.
Command mode, which the house standard says every SDC program should use, is
where VCCA is meant to be trusted, and even there the BUSY timings are guesses.
The WD1793 model is global, so a CoCoSDC and an FD-502 cannot both be plugged
in, which a Multi-Pak would allow.

## 10. Tests assert, and prove they can fail (2026-11-12)

The page-driving tests (ui12, tape, tape3, tape4, tape5) printed what they saw
and failed only on a crash or a page error. They now check each result and exit
with the number of failures (`test/check.mjs`). The CoCoSDC tests run on the
repository's own test ROM (`test/sdcrom.asm` as the system ROM), so they need no
Tandy ROM; each of their checks was shown to fail on a deliberately broken
build (13 breaks of the core for `sdc`, 6 of the front end for `sdcui`).
