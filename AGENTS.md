# Project & Agent Instructions

VCCA: a Tandy CoCo 3 emulator for Android. Owner: CoCoScene. GPL-3.0-or-later.
**Scope:** only VCCA. Games run *in* it (Balloon Fighter, Beast in the Shadow…) live in their own repos and are not changed from here.

## Toolchain & Verification
- **Project Type & Stack:** Android CoCo 3 emulator (VCCA): VCC's C++ core (`src/core`, glue in `src/port`) compiled to WebAssembly (clang 18, wasm32-wasi), HTML/JS front end (`src/web`) in an Android WebView (`src/android`); APK/AAB from the Ubuntu SDK tools + bundletool
- **Build & Test Command:** `./build.sh && test/run.sh` (`VCCA_KEYSTORE_PASS=… ./build.sh` also builds and verifies `dist/VCCA.apk` and `dist/VCCA-play.aab`; most tests need `roms/coco3.rom` and `roms/disk11.rom`). Both must pass before a commit; read results from their output, not from a document. A keystore build rewrites `dist/`: `git checkout dist/` unless releasing.
- **Emulator Verification:** For CoCo binaries, test changes in `xroar-dev` (`~/.xroar`) before marking a task complete. VCCA itself is verified by `test/run.sh`.
- **CoCo Hardware Corpus:** For any 6809/6309 assembly, GIME, VDG, SAM, PIA, BASIC, or disk work, read from `$HOME/CoCo-Skills` (pre-installed by the container setup script—do NOT re-clone if that folder exists). Only if `$HOME/CoCo-Skills` is missing, run `gh repo clone CarlosCamacho/CoCo-Skills "$HOME/CoCo-Skills" -- --depth 1` (or construct the standard GitHub HTTPS clone URL for `CarlosCamacho/CoCo-Skills`). Start with `coco-router/SKILL.md` and load only the specific child skill files needed for this task (`coco-hardware`, `coco-tooling` → `coco-project-organization` for this repo's layout).

## Standing Rules
- **CoCoSDC Required:** Every CoCo program must support CoCoSDC (and DriveWire via DSKCON) and must never hang on a machine without a disk controller, unless explicitly excluded.
- **Hardware Discipline:** Preserve Direct Page (`DP`) state across interrupts/subroutines and strictly respect 8KB MMU bank boundaries.
- **Context Hygiene:** Keep edits focused strictly on the assigned task. Do not refactor or rewrite unrelated files.
- **Authorship:** commits by `Carlos Camacho <idevgames@gmail.com>`, with no Co-Authored-By or session-link trailers (the public contributor list shows only the owner).
- **Never commit** the signing keystore or its password, or any ROM (Tandy's and Microsoft's). Check `git status`.
- **Branches:** work on `dev/<topic>`; reach `main` by pull request when the owner asks. `main` must build and pass `test/run.sh`.
- **Version** lives in `VERSION` (`build.sh` fails if `src/web/app.js` disagrees); bump `android:versionCode` in `src/android/AndroidManifest.xml` every release. Releases are tags `vX.Y.Z`, published by the owner with `dist/VCCA.apk` attached (this environment cannot push tags).
- **Save states** load only in the build that made them (hash of `vcc.wasm`): any `src/core`/`src/port` change breaks them; say so in release notes.
- **`src/core` is VCC's source**, edited only where Windows got in the way or where it is wrong. Each emulation fix gets a test ROM in `test/` that fails before the fix (as `vbord`, `timer`) and cites its evidence (XRoar source, the hardware skill): the harness agrees with whatever the core does.
- **Every check must be proved:** break the code it covers and watch it fail. A skipped test is not a test.
- **Packages:** the GitHub APK keeps `com.vcce.vcc` (installs over every release); the Play build is `com.cocoscene.vcca`, permanent once uploaded.
- **Evidence:** never write "tested" without the venue (Chromium via `test/run.sh`, a named Android device, BlueStacks, real hardware: none so far). Log runs in `docs/testing.md`, lessons under `### Learned` in `CHANGELOG.md`, decisions in `docs/decisions.md`. Open work is GitHub Issues.

## Mandatory Session Handoff Rule
- Before ending your session, closing out, or opening a PR, you MUST overwrite `HANDOFF.md` in the repo root (strictly under 35 lines) with:
  1. Current branch/commit status and what builds cleanly
  2. Immediate next backlog task
  3. Dead ends / failed approaches tried in this session that must not be repeated
- Always include the updated `HANDOFF.md` in your commit/PR.
