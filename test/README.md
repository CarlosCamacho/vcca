# Tests

Run `./build.sh`, then `test/run.sh`. It serves `src/web` with
`build/vcc.wasm` on http://127.0.0.1:8765/, runs the tests below in Chromium
(Playwright) or straight against `vcc.wasm`, and exits 1 if any fails. Logs go
to `build/test-logs/`. Needs `roms/coco3.rom` and `roms/disk11.rom` (or
`$VCCA_ROMS`); screenshots go to the system temp folder (or `$VCCA_TMP`).

**Venue:** every result from these is "Chromium on a desktop" or "Node running
`vcc.wasm`" — not an Android device and not a real CoCo 3. Record results in
`docs/testing.md`.

## Run by `test/run.sh`

| Test | Checks | Pass means |
|---|---|---|
| `timer` | GIME timer in scanline mode, with `timerrom.raw` | every border band is the same height (prints `PASS`) |
| `vbord` | GIME VBORD timing, with `vbordrom.raw` | the border colour changes exactly where the bottom border begins (prints `PASS`) |
| `ui12` | printer capture, joystick ports, function keys | no page errors; also prints the values it read |
| `tape` | CSAVE/CLOAD, CSAVEM/CLOADM, `.wav` loading through Run | no page errors; prints what loaded |
| `tape3` | CSAVE to a blank WAV tape, then Run of the same program re-encoded as a 22 kHz stereo capture, real time and fast | no page errors; prints each result |
| `tape4` | the tape deck in the menu and the status line during CLOAD (screenshots) | no page errors |
| `tape5` | FWD to the second program on `.cas` and `.wav` tapes | no page errors; prints which program loaded |

⚠ The page-driving tests fail on a page error or a crash, but most of what
they check is printed for a person to read rather than asserted. Read
`build/test-logs/*.log` after a change to the area they cover. Turning those
printouts into assertions is open work.

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
| `soundrom` | every sound cartridge at once |
| `sscrom` | Speech/Sound Pak speech ("HELLO") |
| `timerrom` | GIME timer bands (for `timer`) |
| `vbordrom` | VBORD border change (for `vbord`) |

`ff16k.rom` is a 16K filler cartridge image and `test.dsk` a small disk, both
made for these tests.
