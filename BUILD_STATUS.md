## 0.6.0-hp-2bit-preview

Correct P1006 native video packing to 2 bits/pixel while accepting 600-dpi input.
Native two-page A4/Letter output matches foo2xqx reference exactly; 100-page
allocation accounting and dense/OOM tests pass. Physical blank-page fix pending
retest; firmware build/deploy is checked in GitHub Actions.

## v0.5.0-diagnostics-preview

Automatic read-only diagnostics enabled in MODE=normal. Firmware builds for
ESP32-S3 N16R8; physical log/export/reset tests remain pending. Open /diagnostics
or use the sidebar link and download printserver-log.txt after a failed print.

## v0.4.0-standard-preview

Build: ESP32-S3 N16R8, 1,720,419 app bytes, 77,092 static RAM bytes.
Added PCL5 and PostScript mono streaming fallback. +3,268 app bytes vs v0.3.
Native 100-page allocation checks, HP dense pages/OOM, PCL decoding and Ghostscript
PS rendering passed; see VALIDATION.md for exact coverage and measurements.
Physical printing/USB/Wi-Fi heap verification and QEMU remain unverified.

## v0.3.0-dual-preview

HP P1006 driver and user-supplied firmware bootstrap added. Physical verification
pending for both owned printers. See README for /hp-firmware setup and limitations.

# Build status — 0.2.0-web-preview

ESP32-S3 N16R8 firmware compilation: PASS (Espressif32 6.12.0 / ESP-IDF 5.5.0).
Application image: 1,690,235 bytes of the 4 MB app slot (40.3%), plus symbols.
Improv Serial provides browser Wi-Fi setup over the UART/COM connector.

Web flash parts are derived from an esptool-patched image and omit the NVS
range [0x9000, 0xf000), preserving credentials when erase is not selected.
The app remains at 0x20000, and initial OTA selection is at 0xf000.

No physical printer or board was attached. USB flashing, Wi-Fi radio connection
and paper output remain pending real hardware validation.
