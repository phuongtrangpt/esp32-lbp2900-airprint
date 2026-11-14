## v0.4.0-standard-preview

Build: ESP32-S3 N16R8, 1,720,387 app bytes, 77,092 static RAM bytes.
Added PCL5 and PostScript mono streaming fallback. +3,236 app bytes vs v0.3.
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
