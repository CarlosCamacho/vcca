# VCCA technical notes

## Layout of the code

```
src/core/      VCC 2.1.10.0's sources, edited only where Windows got in the way
               or where emulation was wrong (CPUs, GIME, PIAs, FD-502, S/SC, GMC)
src/compat/    a minimal Windows.h and friends
src/port/      host.cpp   reset sequence, frame and audio hand-off, exported calls
               pak.cpp    Multi-Pak and cartridges (VCC's, plus MAME's Stereo
                          Composer, Symphony 12, CoCo PSG and Glenside IDE, ported)
               sdc.cpp    the CoCoSDC: VCC's SDC command mode, on the front end's SD card
               tape.cpp   VCC's Cassette.cpp, reading the front end's tape file
               vfs.cpp    Win32 file calls answered by the front end
               cxa.c      C++ runtime stubs for a build without exceptions
src/web/       the front end: screen, keyboard, joystick, audio, menus, help
src/android/   manifest, MainActivity.java, launcher icons
```

## WebAssembly core

`build.sh` compiles `src/core` and `src/port` with clang for `wasm32-wasi`
into `build/vcc.wasm`, a reactor module (no `main`). The front end calls its
exports (`vcc_init`, `vcc_run_frame`, `vcc_status`, `vcc_tape_*`, …) and supplies
imports from module `vcca` for files (`file_open/size/read/write/close`).

Disk, tape and hard drive images live on the JavaScript side, so a save
state — a copy of the WebAssembly memory — stays small and never rolls back a
disk. A save state loads only in the build that made it, checked by a hash of
`vcc.wasm`.

## Audio

Cartridge audio follows VCC: two 8-bit channels packed in a word, the high
byte to the right speaker. Every cartridge with a sound output says so
(`HasAudio()`), because a sample of `$00` is a real level, not silence. The
front end plays it through an AudioWorklet (`audio-worklet.js`).

## GIME timing that differs from upstream VCC

- **Timer, scanline mode (TINS=0):** decrements on HSYNC (`TimerHSync`, from
  `HSYNC(0)`), so intervals are whole lines. The 279 ns mode keeps VCC's
  nanosecond countdown. Reload is count + 1 (1987 GIME) or + 2 (1986).
- **VBORD:** raised as the active area ends and the bottom border begins, not
  at vertical sync. The PIA field sync stays at vertical sync.

Both have test ROMs: `test/timerrom.asm`, `test/vbordrom.asm`.

## Android shell

`MainActivity` is a full-screen WebView. Assets are served from
`https://appassets.androidplatform.net/` by `shouldInterceptRequest`, which
gives the page a secure context (needed for AudioWorklet) without `file://`.
Links out of the app open in the browser. A JavaScript interface
(`AndroidHost`) stores settings in SharedPreferences and saves files through
the system file picker.

Two packages from one source (`tools/build-android.sh`):

| | `VCCA.apk` | `VCCA-play.aab` |
|---|---|---|
| For | GitHub releases, sideloading | Google Play |
| Runs on | Android 5.0+ (API 21) | Android 7.0+ (API 24) |
| Targets | API 34 | API 36 |
| Package | `com.vcce.vcc` | `com.cocoscene.vcca` |

Both compile against `android-23.jar` from the Ubuntu SDK packages. The Play
build's newer behavior — Back through `OnBackInvokedCallback` on Android 16,
padding clear of the system bars when drawn edge to edge on Android 15+ — goes
through reflection, switched on only when both the device and the package's
target call for it. The front end needs a WebView of Chrome 80 or later
(`??`, and WebAssembly bulk memory from Chrome 75); `index.html` says so on an
older one instead of showing a blank screen.

APK builds are reproducible: aapt2 dates every entry 1980-01-01, and the build
gives `classes.dex` the same date.

## CoCoSDC

`src/port/sdc.cpp` holds the card; `pak.cpp` makes it cartridge type 13.

- **Command mode** (latch `$43` at `$FF40`) is VCC's SDC simulator (EJ Jaquay,
  2025) with the host side replaced: sector reads and writes in word and byte
  mode, `Q`/`I`/`C`/`V`/`>`/`+` replies, and the `M:`, `m:`, `N:`, `n:`, `D:`,
  `L:`, `K:`, `R:`, `X:` blocks. Not ported: 512-byte streaming (`$90`,
  `$D0`), SDF images, flash programming, the Cloud-9 clock.
- **BUSY.** The card answers BUSY for a few scanlines after every command
  (2 after a read or reply, 16 after a mount or directory change, 32 after a
  sector write), and bytes of a block sent before READY are lost. VCC's
  simulator answers at once. The real card's timings are not published:
  these numbers are a guess, chosen so a program that does not wait fails.
- **Errors.** A read past the end of an image or of an empty drive returns
  FAILED + `$10`; a write to a read-only file FAILED + `$40`; a missing file
  FAILED + `$10`; an image already mounted in the other drive FAILED + `$20`.
- **Floppy personality** (any other latch value) is the FD-502's WD1793 model
  (`wd1793.cpp`) with the card's two images as drives 0 and 1: reads, writes,
  BUSY, DRQ and NMI as that model does them, not VCC's SDC floppy emulation
  (no writes, no BUSY, DRQ held past the 256th byte). The WD1793 model is
  global, so the CoCoSDC and an FD-502 exclude each other.
- **Flash.** Eight 16K banks; bank 0 is the slot's ROM (SDC-DOS, or Disk BASIC
  1.1 when none is given), bank 1 an optional second ROM. `$FF43` selects a
  bank and reads back as bank | (`$FF42` & `$F8`), which is what the
  designer's detection probe tests.
- **The SD card** is held by the front end (`src/web/sdcard.js`): each file
  is "sd:PATH" in the shared file table, opened through the same `file_*`
  imports as any disk; folders and changes go through `sd_list`, `sd_mkdir`,
  `sd_rename`, `sd_delete` and `sd_create`. The app stores each file in
  IndexedDB as "sdf:PATH" and lists files and folders in its settings.
