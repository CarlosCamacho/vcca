# VCCA — working agreement

VCCA is a Tandy Color Computer 3 emulator for Android: VCC's emulation core
(C++) compiled to WebAssembly, run in an Android WebView with an HTML/JS front
end. Owner: CoCoScene. GPL-3.0-or-later.

**Scope:** only VCCA. Games run *in* it (Balloon Fighter, Beast in the Shadow,
anything else) live in their own repositories and are not changed from here.

## Skills

`coco-router` first for anything about the CoCo itself, then `coco-hardware`
(GIME, PIA, SAM behavior) and `coco-tooling` (emulator fidelity, test design,
this repository's layout: `coco-project-organization`).

## Build and test

```
./build.sh                          # build/vcc.wasm
VCCA_KEYSTORE_PASS=… ./build.sh     # + dist/VCCA.apk, dist/VCCA-play.aab, verified
test/run.sh                         # needs roms/coco3.rom, roms/disk11.rom
```

Both must pass before a commit. Take results from their output, not from a
document. A `./build.sh` with the keystore rewrites `dist/`; unless you are
cutting a release, `git checkout dist/` afterwards.

## Rules with reasons

- **Commits are authored `Carlos Camacho <idevgames@gmail.com>`, with no
  Co-Authored-By or session-link trailers.** The owner asked for this; the
  public contributor list shows only the owner.
- **Never commit the keystore or its password, or any ROM.** The key signs
  every update (a different key cannot update installed copies); the ROMs are
  Tandy's and Microsoft's. `.gitignore` covers both; check `git status` anyway.
- **Work on a branch (`dev/<topic>`) and arrive on `main` by pull request**
  when the owner asks for one. `main` must build and pass `test/run.sh`.
- **The version is `VERSION`.** `build.sh` fails if `src/web/app.js` disagrees.
  Bump `android:versionCode` in `src/android/AndroidManifest.xml` with every
  release: it only goes up.
- **Save states load only in the build that made them** (keyed by a hash of
  `vcc.wasm`). Any change to `src/core` or `src/port` breaks them; say so in
  the release notes.
- **`src/core` is VCC's source**, edited only where Windows got in the way or
  where it is wrong. Each fix to emulation gets a test ROM in `test/` that fails
  before the fix (as `vbord`, `timer`), and cites its evidence (XRoar source,
  the hardware skill), because the harness agrees with whatever the core does.
- **The GitHub APK keeps package `com.vcce.vcc`** so it installs over every
  earlier release; the Play build is `com.cocoscene.vcca`, permanent once
  uploaded.
- **Releases are tagged `vX.Y.Z`** and published by the owner in the GitHub UI
  (this environment cannot push tags), with `dist/VCCA.apk` attached.

## Evidence

Never write "tested" without the venue: Chromium via `test/run.sh`, a named
Android device, BlueStacks, or real hardware (none so far). Record results in
`docs/testing.md`; what a change taught goes under `### Learned` in
`CHANGELOG.md`; decisions in `docs/decisions.md`.
