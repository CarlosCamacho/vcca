# Changelog

VCCA's own version numbers, newest first. The emulation core is built from
VCC 2.1.10.0 throughout. "Venue" says where a result was seen: nothing here
has been checked on a real CoCo 3, and Android results name the device.

## [Unreleased]
### Added
- **The on-screen stick drawn as a real controller**: the TRS-80 Deluxe Joystick (the new
  default), the Radio Shack joystick or a Wico, from the art in XRoar for Android, ported to the
  browser's canvas (`src/web/joyart.js`). It stays analog. The red button on the case is button 1
  and the Wico's white tip button 2; FIRE and 2 beside it still work. Choosing a look sets the
  stick's centering to the real controller's (the Radio Shack stick stays put). "Simple" keeps
  the old stick. `test/joyart.mjs` (26 checks, no ROM needed); `tools/joyart-preview.mjs`
  renders the art as XRoar's `tools/artpreview` does.
- **CoCoSDC** (cartridge type 13): the SD card controller, with an SD card of disk images and
  folders kept on the device and managed in ☰ → Cartridge → SD card. Command mode is VCC's SDC
  simulator (EJ Jaquay, 2025) ported onto VCCA's file system: sector reads and writes in word and
  byte mode, image size, mount (`.DSK` added, wildcards, folders), new image, directory listing,
  change, make, rename and delete, and `STARTUP.CFG` at power-on. The floppy personality is the
  FD-502's WD1793 model with the card's two images as drives 0 and 1, so Disk BASIC 1.1 reads and
  writes through it. The card's ROM is SDC-DOS when given, else Disk BASIC 1.1. Run… with a `.dsk`
  puts it on the card and mounts it. `test/sdc.mjs` (26 checks) and `test/sdcui.mjs` (14) run on
  the repository's own test ROM and need no Tandy ROM. Not ported: 512-byte streaming, SDF
  images, flash programming.
- The CoCoSDC holds BUSY for a few scanlines after each command, and loses block bytes sent
  before READY, so a program that does not wait for the card fails here as on one. VCC's simulator
  answers at once. The timings are a guess (docs/decisions.md 9).
### Changed
- The page-driving tests assert their results instead of printing them for a person to read:
  `ui12`, `tape`, `tape3`, `tape4` and `tape5` exit with their number of failures, and each fails on
  a deliberately broken front end.
- `test/sdcbasic.mjs`: Disk BASIC 1.1 through the CoCoSDC's floppy personality (DSKINI0, SAVE,
  DIR, LOAD, RUN on an SD card image).
- The Node harnesses (timer, vbord, emes, snd2) answer any host import they do not provide with
  -1, so a new import cannot break them again.
- `test/tape.mjs` makes its WAV as a real recording sounds, with half a second of silence before
  each block's leader.
- `test/run.sh` runs the tests that need no Tandy ROM even without them, and exits 1 when any test
  could not run; a test fails on a line starting `FAIL` (a check that merely mentions the card's
  FAILED bit no longer counts).
- The CoCoSDC and the FD-502 exclude each other: both are the WD1793 model.
- Changes in `src/port` break save states made by earlier builds.
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
- **Asserting the page tests found two checks that had never passed.** `tape`'s CLOAD step waited
  for the screen to end in `OK`, but the screen copy ends in the cursor; and its WAV, synthesised
  from the CAS bytes with no pause between blocks, loaded the header and lost the program, because
  BASIC's motor-on delay swallowed the data block's leader. Both printed `TIMEOUT` into a log nobody
  read; `main` fails both the same way [Chromium 1243, the owner's ROMs].
- **The CoCoSDC commit broke `timer` and `vbord`**, which instantiate `vcc.wasm` with a fixed list of
  host imports, and it went unseen because those tests could not run without the Tandy ROMs. Found
  the moment the ROMs arrived. A test that is skipped is a test that is not there.
- **A CoCo 3 shows "DISK EXTENDED COLOR BASIC 2.1" over disk11.rom** (its ROM patches the banner),
  so a check for "1.1" fails [VCCA, the owner's ROMs].
- **The Wico's tip button covered most of its ball.** XRoar for Android takes a touch within 1.6
  times the tip's radius as the tip, which leaves only a thin ring of the ball to grab the stick
  by. VCCA takes the tip's own size (1.1 times). Found by `test/joyart.mjs` [Chromium 1243]; the
  XRoar app has the same zone.
- **The art matches XRoar's**: rendered at XRoar's preview size, the canvas port differs from
  `tools/artpreview`'s PNGs by 2.8-3.6/255 on average, the plastic grain and the font being the
  rest [Chromium 1243 vs Java2D].
- **VCC's WD1793 model ends a sector read on a 257th read of the data register or on a time-out**,
  and the time-out sets LOST DATA: a program that reads exactly 256 bytes, as a real WD1773 wants,
  finishes with status `$04` here. Seen through the CoCoSDC's floppy personality in `test/sdc.mjs`
  [SIM]; it is the FD-502 code VCCA has always used, so plain floppies do it too. Not fixed in this
  change; a real WD1773 sets nothing after the 256th byte.
- **The designer's CoCoSDC probe** (`$64` to `$FF42`, read `$FF43`, clear, compare) works against
  VCC's flash-register model: `$FF43` must read bank | (`$FF42` & `$F8`) [SIM, from VCC's source].
- **The first CoCoSDC test passed every check on its first run, and two of its checks could not
  fail**: a mount that should have appended `.DSK` was rescued by the wildcard fallback, and the
  "bytes before READY" rule was never exercised. Found by breaking the code on purpose; the test
  gained a decoy `TWO.BAK` and a write that does not wait.
- **A collapsed `<details>` section has empty `innerText`**: the SD card manager test read nothing
  until it opened the section, as a user would [Chromium 1243].
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
