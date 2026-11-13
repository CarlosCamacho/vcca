# Project & Agent Instructions

## Toolchain & Verification
- **Project Type & Stack:** Android CoCo 3 emulator (VCCA): VCC's C++ core compiled to WebAssembly (clang 18, wasm32-wasi), HTML/JS front end in an Android WebView; Android packages from the Ubuntu SDK tools + bundletool
- **Build & Test Command:** `./build.sh && test/run.sh` (APK/AAB too with `VCCA_KEYSTORE_PASS=… ./build.sh`; tests need `roms/coco3.rom` and `roms/disk11.rom`)
- **Emulator Verification:** For CoCo binaries, test changes in `xroar-dev` (`~/.xroar`) before marking a task complete.
- **CoCo Hardware Corpus:** For any 6809/6309 assembly, GIME, VDG, SAM, PIA, BASIC, or disk work, read from `$HOME/CoCo-Skills` (pre-installed by the container setup script—do NOT re-clone if that folder exists). Only if `$HOME/CoCo-Skills` is missing, run `gh repo clone CarlosCamacho/CoCo-Skills "$HOME/CoCo-Skills" -- --depth 1` (or construct the standard GitHub HTTPS clone URL for `CarlosCamacho/CoCo-Skills`). Start with `coco-router/SKILL.md` and load only the specific child skill files needed for this task.

## Standing Rules
- **CoCoSDC Required:** Every CoCo program must support CoCoSDC (and DriveWire via DSKCON) and must never hang on a machine without a disk controller, unless explicitly excluded.
- **Hardware Discipline:** Preserve Direct Page (`DP`) state across interrupts/subroutines and strictly respect 8KB MMU bank boundaries.
- **Context Hygiene:** Keep edits focused strictly on the assigned task. Do not refactor or rewrite unrelated files.

## Mandatory Session Handoff Rule
- Before ending your session, closing out, or opening a PR, you MUST overwrite `HANDOFF.md` in the repo root (strictly under 35 lines) with:
  1. Current branch/commit status and what builds cleanly
  2. Immediate next backlog task
  3. Dead ends / failed approaches tried in this session that must not be repeated
- Always include the updated `HANDOFF.md` in your commit/PR.
