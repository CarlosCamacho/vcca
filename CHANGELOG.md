# Changelog

VCCA's own version numbers, newest first. The emulation core is built from
VCC 2.1.10.0 throughout. "Venue" says where a result was seen: nothing here
has been checked on a real CoCo 3, and Android results name the device.

## [Unreleased]
### Added
- **emesDOS09 smart cartridge** (cartridge type 12): the 16 KiB ROM in two 8 KiB pages
  (page = `$FF40` bit 7) and the controller mailbox at `$FF40`-`$FF4F`, with an SD card image
  as the file `emessd` (read-only for now). The controller is the emesDOS09 firmware core,
  copied unmodified into `src/port/emes/` at emesDOS09 commit `7c3115d` and run through
  `host_mailbox.c`. Choose it in a Multi-Pak slot, then pick the ROM and the SD image.
  `test/emes.mjs` runs a 6809 program against it: IDENTIFY (E9CT, protocol 2.2), VOLUME_MOUNT
  and the root directory of `dist/fat16-superfloppy-sprint47-rc2.img` (5 entries, as the C host
  test finds). With a plain ROM pak in its place every check fails.
- Changes in `src/port` break save states made by earlier builds, as every core change does.

### Learned
- **A CoCo 3 does not leave a cartridge's `$C000`-`$DFFF` alone at boot.** With `coco3.rom` and the
  emesDOS09 ROM in the slot, RAM was writable at `$D000` after boot and the cartridge had been read
  8,194 times (8,192 plus the two `DK` bytes) before the first frame ended, so Super BASIC's reset
  had copied the first 8 KiB into RAM. Bank-switching `$FF40` then changes nothing the CPU sees, and
  the emesDOS09 ROM, which fetches its resident page from bank 1, stops at `FATAL RAM`. Seen in
  VCCA only (VCC 2.1.10 core); not checked against a real CoCo 3. The cartridge is therefore
  exercised from a program, not booted (`test/emes.mjs`).
### Changed
- Repository reorganized to the CoCo project standard: sources under `src/`,
  scripts under `tools/`, one `./build.sh` that builds and verifies both
  Android packages, `test/run.sh`, `docs/`, `dist/`, this file and
  `CLAUDE.md`. The version lives in `VERSION`. No change to the app: the
  rebuilt `VCCA.apk` holds the same files as the 1.3.0 release, byte for byte.
- APK builds are reproducible: the same source makes the same file.

## [1.3.0] — 2026-10-10
### Added
- Two Android builds from one source: `VCCA.apk` (GitHub, Android 5.0 and
  later, package `com.vcce.vcc`, targets API 34) and `VCCA-play.aab` (Google
  Play, Android 7.0 and later, package `com.cocoscene.vcca`, targets API 36).
- `PRIVACY.md`, the policy Google Play asks for.
- A WebView too old to run VCCA (before Chrome 80) shows how to update it
  instead of a blank screen.
### Changed
- Back with no menu open leaves VCCA running in the background on every
  Android version; on Android 11 and older it used to close it.
- Android 16 Back handling (`OnBackInvokedCallback`) and padding clear of the
  system bars when Android 15+ draws the app edge to edge, both switched on
  only in the Play build.
- The Android port is credited to CoCoScene.
### Learned
- Google Play requires API 36 for new apps and updates from 2026-08-31, and an
  App Bundle; a personal developer account opened after November 2023 must
  run a 12-tester, 14-day closed test first.
- `app.js` needs Chrome 80 (the `??` operator; `vcc.wasm` needs bulk memory,
  Chrome 75). Venue: esbuild target check and the wasm `target_features`
  section, not an old device.
- Untested on a device: the Play build's Back and edge-to-edge code (needs
  Android 15/16), and anything on Android 5 or 6.

## [1.2.1] — 2026-10-07
### Fixed
- GIME VBORD fires as the picture ends and the bottom border begins, not at
  vertical sync. Programs counting scanlines from it were 21 lines off.
### Learned
- From VBORD to the first visible line is 63 lines in a 200-line mode
  (263 − 200 in XRoar's `tcc1014.c`); VCC had 40. The PIA's field sync does
  fall at vertical sync. Venue: XRoar 1.12.1 source and `test/vbord.mjs`;
  the Shadow of the Beast plains prototype measured in an instrumented VCCA.
- Still open: VCCA shows that prototype's palette changes one line earlier
  than XRoar. Only real hardware can say which is right.

## [1.2.0] — 2026-10-07
### Fixed
- The GIME timer in scanline mode counts HSYNC pulses, so a handler that
  reloads it no longer adds its own latency to every interval.
### Added
- GIME chip setting, 1987 (default) or 1986: the two fire the timer one
  scanline apart.
### Learned
- With a 20-line timer, bands came out 22 and 23 lines in one frame before
  the fix; now every band is N + 2 lines on a 1987 GIME and N + 3 on a 1986.
  XRoar emulates the 1986 chip. Venue: `test/timer.mjs`.

## [1.1.1] — 2026-10-05
### Fixed
- ENTER on a physical keyboard: BlueStacks sends key down and up a
  millisecond apart, shorter than the CoCo's keyboard scan. Key changes now
  go through one queue, at least three frames apart.
- Video recordings open: MP4 only with H.264, otherwise WebM with VP8.
- No more repeated empty recordings: buttons drop focus after a tap.
### Added
- Key test under Configuration → Keyboard, for bug reports.
### Learned
- Asked for plain `video/mp4`, Chromium's MediaRecorder writes VP9 in MP4,
  which many players refuse. Venue: the owner's BlueStacks install.

## [1.1.0] — 2026-10-05
### Changed
- VCCA has its own version numbers; "2.1.9-android10" read as "for
  Android 10". About shows the VCC version separately.
- New app icon and logo; Report a bug in Help opens the GitHub bug form with
  the version and device filled in.

## [1.0] — 2026-10-05 (released as 2.1.9-android10)
### Added
- First release: the VCC core compiled to WebAssembly in an Android WebView,
  with a touch keyboard and joystick, the Commands tab, save states,
  screenshots and video, floppy and IDE hard drives, a cassette deck,
  BitBanger printer capture, and the Multi-Pak sound cartridges (VCC's, plus
  MAME's Stereo Composer, Symphony 12, CoCo PSG and Glenside IDE).
### Learned
- In a Multi-Pak mixer a sample of `$00` is a real level, not silence: each
  cartridge reports whether it has audio.
