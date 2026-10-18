# VCCA design

What VCCA is for and how it behaves. The architecture is in
[technical.md](technical.md); the reasons behind choices in
[decisions.md](decisions.md).

## Purpose

Run CoCo 3 software on an Android phone or tablet with the accuracy of VCC,
the Windows emulator, and the conveniences of XRoar: load or run a program in
one step, play with a touch joystick, type BASIC on a touch keyboard. VCCA
exists for people who write CoCo 3 programs as much as for people who play
them, so emulation fixes come with a test that measures them.

## Machine

- CoCo 3 with a 6809 or 6309, 128K to 8 MB, RGB or composite monitor.
- GIME chip 1987 (default) or 1986: they fire the timer one scanline apart.
- Normal or fast-as-possible speed; double-speed overclock (F3/F4).

## Loading, like XRoar

**Load…** and **Run…** take `.bin`, `.dsk`, `.cas`, `.wav`, `.rom` and `.ccc`.
Run reads the disk directory or tape header and types the right `RUN"…"`,
`LOADM"…":EXEC`, `CLOAD` or `CLOADM:EXEC`.

## Storage

- Four FD-502 drives (`.dsk`), blank disks, saving images back to the device.
- Cassette deck laid out like a tape player: PLAY, REC, STOP, REW, FWD (to the
  next program). `.cas` and any `.wav`; VCC's fast loading.
- IDE hard drives through HDB-DOS, as numbered virtual floppies.

Disk, tape and hard drive images stay on the device between runs.

## Cartridges (Multi-Pak)

FD-502, program paks, Orchestra-90 CC, Speech/Sound Pak, Game Master
Cartridge, Stereo Composer, Symphony 12, CoCo PSG, Glenside IDE at `$FF70` or
`$FF50`.

## Input

- On-screen CoCo 3 keyboard in the XRoar layout, sticky SHIFT/CTRL/ALT.
- Physical keyboards with VCC's special keys (F3–F9, F11).
- Left and right joystick ports, as in VCC: on-screen stick, game controller,
  touch on the screen, keyboard, or none; standard, Tandy Hi-Res or CC-MAX.
- Landscape puts stick, screen and fire buttons side by side, swappable.
- **Commands** tab: one-tap BASIC commands and seven user slots.

## Output

Save states, PNG screenshots, video with sound (MP4/H.264 where the device has
it, WebM otherwise), BitBanger printer capture to text, copy the screen's text.

## Menus

Grouped like VCC's menu: File, Edit, Configuration, Cartridge, Help, as
accordions. Help covers every feature, with step-by-step hard drive setup, and
**Report a bug** opens the GitHub form with version and device filled in.

## Not ported yet

The Becker port and DriveWire, VCC's VHD hard disk pak, the CoCoSDC, the
RS-232 pak and VCC's debugger windows.
