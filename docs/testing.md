# Test log

Every entry names its venue. **Nothing has been run on a real CoCo 3**, and
the only Android devices reported so far are the owner's.

| Date | Build | Venue | Tester | Result |
|---|---|---|---|---|
| 2026-10-18 | 1.3.0 (reorganized tree) | Chromium (Playwright), `test/run.sh` | Carlos Camacho | timer, vbord, ui12, tape, tape3, tape4, tape5 pass. Rebuilt `VCCA.apk` has the same files as the 1.3.0 release. |
| 2026-10-10 | 1.3.0 | Chromium (Playwright) | Carlos Camacho | ui12 and vbord pass; too-old-WebView message appears only when AudioWorklet is removed. |
| 2026-10-10 | 1.3.0 | bundletool 1.18.3 `validate`, `aapt dump badging` | Carlos Camacho | Play bundle valid; packages, versions and SDK levels as intended. **Not run on any Android device.** |
| 2026-10-07 | 1.2.1 | `test/vbord.mjs` (Node + `vcc.wasm`) | Carlos Camacho | Border changes at line 218, where the bottom border starts. Fails on 1.2.0. |
| 2026-10-07 | 1.2.1 | instrumented VCCA, Beast plains prototype | Carlos Camacho | Raster events on their intended lines at K+00 T+00, GIME 1986; palette changes one line earlier than XRoar's screenshot. |
| 2026-10-07 | 1.2.0 | `test/timer.mjs` | Carlos Camacho | Every band N + 2 lines (1987) / N + 3 (1986); before the fix 22 and 23 in one frame. |
| 2026-10-05 | 1.1.0 | BlueStacks (owner's laptop) | owner | Physical ENTER did nothing; MP4 recording saved repeated 0 KB files and the recordings would not open. Fixed in 1.1.1, which has not been re-reported. |
| before 1.0 (date not recorded) | pre-release | owner's tablet | owner | Fire buttons missing in landscape; fixed before the first release. |

## Not tested

- The Play build's Android 16 Back handling and Android 15+ edge-to-edge
  padding: no Android 15 or 16 device has run it.
- Android 5 and 6, which the GitHub APK allows since 1.3.0.
- Any emulation behavior against real hardware.
