# Tests

Run `./build.sh`, then `test/run.sh`. It serves `src/web` with
`build/vcc.wasm` on http://127.0.0.1:8765/, runs the tests below in Chromium
(Playwright) or straight against `vcc.wasm`, and exits 1 if any fails or could
not run. Logs go to `build/test-logs/`. `sdc` and `sdcui` need no Tandy ROM;
the rest need `roms/coco3.rom` and `roms/disk11.rom` (or `$VCCA_ROMS`).
Screenshots go to the system temp folder (or `$VCCA_TMP`).

Every test asserts what it checks, prints `ok` or `FAIL` per check, and exits
with the number of failures (`check.mjs`). A new check is proved by breaking
the code it covers and watching it fail.

**Venue:** every result from these is "Chromium on a desktop" or "Node running
`vcc.wasm`" — not an Android device and not a real CoCo 3. Record results in
`docs/testing.md`.

## Run by `test/run.sh`

| Test | Checks | Pass means |
|---|---|---|
| `sdc` | the CoCoSDC through its registers, with `sdcrom.raw` as the machine's ROM: detection, flash banks, command-mode reads (word and byte mode), a write, errors, write protect, mounting, a new image, the listing, BUSY, the floppy personality | 26 checks |
| `sdcui` | the CoCoSDC in the app: the SD card's files reach the CoCo, its writes and new images reach IndexedDB, the SD card manager, Run… with a `.dsk`, a reload | 14 checks |
| `timer` | GIME timer in scanline mode, with `timerrom.raw` | every border band is the same height (prints `PASS`) |
| `vbord` | GIME VBORD timing, with `vbordrom.raw` | the border colour changes exactly where the bottom border begins (prints `PASS`) |
| `ui12` | printer capture, joystick ports, function keys | the printout is exact; JOYSTK values in range for each port; F4 sets overclock 4; F6 switches the monitor |
| `tape` | CSAVE/CLOAD, CSAVEM/CLOADM, `.wav` loading through Run | each step finishes; the header says HELLO; `$400` = `$2A` after CLOADM:EXEC; the WAV runs; the tape survives a reload |
| `tape3` | CSAVE to a blank WAV tape, then Run of the same program re-encoded as a 22 kHz stereo capture | the audio decodes to WAVY; the re-encoded WAV prints WAV OK |
| `tape4` | the tape deck in the menu and the status line during CLOAD | the deck shows the tape and PLAY; the status line shows `Play n%` |
| `tape5` | FWD to the second program on `.cas` and `.wav` tapes | FWD stops partway; CLOAD then loads SECOND |

⚠ The assertions in `ui12`, `tape`, `tape3`, `tape4` and `tape5` were written on
2026-11-12 in a container without the Tandy ROMs and **have not been run yet**;
their expected values come from those tests' last recorded output.

## Run by hand

| Script | Use |
|---|---|
| `tape2.mjs <file> [fast]` | load a given tape (in the temp folder) with CLOAD and print each step |
| `snd2.mjs <frames> <out-prefix>` | run a game disk (`roms/balloon.dsk`) headless with the sound cartridges chosen by `SLOTS`, and print per-segment audio levels (WAV with `WAV=`/`SEGWAV`, PNG frames with `PNGAT`) |
| `ui11.mjs <w> <h> <dpr> <tag>` | screenshot the layout at a given screen size |

## Test ROMs

`*.asm` are the sources, assembled with LWTOOLS 4.21 (`lwasm --raw -o x.raw
x.asm`); the `.raw` files are committed so the tests run without lwasm, and
reassembling each gives an identical file (checked 2026-10-18).

| ROM | Exercises |
|---|---|
| `testrom` | CPU, SAM/VDG text, PIA keyboard scan, DAC, an FD-502 sector read |
| `sdcrom` | the CoCoSDC, run as the machine's ROM (for `sdc` and `sdcui`) |
| `soundrom` | every sound cartridge at once |
| `sscrom` | Speech/Sound Pak speech ("HELLO") |
| `timerrom` | GIME timer bands (for `timer`) |
| `vbordrom` | VBORD border change (for `vbord`) |

`ff16k.rom` is a 16K filler cartridge image and `test.dsk` a small disk, both
made for these tests.
