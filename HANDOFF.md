# Current Baton Pass
- **Current State:** `main` (0eaeda3, VCCA 1.3.0 + the emesDOS09 cartridge from PR #3) builds `build/vcc.wasm` cleanly with `./build.sh`. The CoCoSDC cartridge, the drawn joystick looks and the asserted test suite are pushed but unmerged on `dev/joystick-art` (which includes `dev/cocosdc`), where all 11 tests pass with the owner's ROMs.
- **Immediate Next Step:**
  - Open and merge the pull request for `dev/joystick-art` into `main`, then release 1.4.0 (bump `VERSION`, `app.js`, `versionCode`).
  - Fix the WD1793 model ending a sector read only on a 257th read or a time-out (sets LOST DATA), with a test ROM that fails first.
- **Dead Ends / Do Not Repeat:** None recorded yet.
- **Verification:** Run `./build.sh && test/run.sh` and confirm zero errors before committing.
