## 0.6.0-hp-2bit-preview

- Correct P1006 native XQX packing to 1200x600 / VIDEO_BPP=2, matching its upstream PPD (client input remains 600 dpi).
- Expand only a 128-row stripe plus two preceding context rows, keeping the original 1-bit page and avoiding an 8+ MB bitmap allocation.
- Added raster ink-pixel count to hardware logs; added independent 2-bit foo2xqx byte comparison in CI.
- A4 and Letter two-page outputs match independent upstream reference exactly; 100-page and dense/OOM memory tests passed natively. Physical paper output requires retest.

## 0.5.0-diagnostics-preview

- Always-on bounded RAM log capture, including normal mode; preserve reset/watchdog RTC tail and core dump summary.
- Read-only /diagnostics page, downloadable /diagnostics.txt report and /diagnostics/coredump on existing HTTP port; sidebar link and auto refresh.
- Include firmware/USB/driver/heap/reset metadata; log job memory boundaries, HP bootstrap, USB timeouts and CAPT command IDs/byte counts without payload dumps.
- Route CAPT stderr diagnostics into ESP_LOG capture; retain native stderr behaviour.
- Clean up partial logger allocation failure; normal diagnostics do not enable debug write endpoints.

## 0.4.0-standard-preview

- Added streaming mono PCL5/PostScript drivers selected by exact IEEE1284 CMD tokens after model-specific drivers. Unsupported host-based/UFRII/SPL/PCLXL-only devices stay refused.
- Added 100-page allocation accounting for CAPT, HP A4/Letter, PCL5/PS, dense-noise HP pages and compression OOM recovery tests.
- Added per-stripe yield and explicit PSRAM allocation in JBIG compression to protect internal Wi-Fi/USB RAM.
- Restore PostScript VM after each page.
- Added per-pixel PCL stream validation and Ghostscript PostScript rendering tests, CI steps, and explicit compatibility/unsupported matrix.

## 0.3.0-dual-preview

- Added HP LaserJet P1006 (03f0:3e17), 600 dpi mono XQX/JBIG, A4/Letter, automatic USB driver matching.
- Added browser upload /hp-firmware and separate persistent partition for user-supplied HPDL; CRC/size checks and pre-job bootstrap, with FWVER skip.
- Added tracked JBIG allocations and OOM recovery, native HP tests and optional independent foo2xqx comparison.
- Kept the common USB web installer and Canon driver. Neither printer physically validated.

# Changelog

## 0.2.0-web-preview

- Add an HTTPS USB web installer using locally bundled ESP Web Tools 10.5.2.
- Add Improv Serial Wi-Fi setup, network scanning and device URL discovery.
- Retain UART text setup while allowing Wi-Fi changes without reinstalling.
- Generate flash assets that exclude the saved-credentials NVS range.
- Publish the installer through GitHub Pages only after CI tests/build pass.
- Physical USB flashing, Wi-Fi setup and paper output still need real hardware validation.

## 0.1.0-capt-preview

- Fork ESP32-S3 AirPrint/IPP and web UI from ngxson/esp32-usb-to-airprint.
- Port LBP2900 CAPT state machine and Hi-SCoA compression from ValdikSS/captdriver.
- Add binary USB replies, bounded CAPT read/write waits and per-job error unwinding.
- Add 600 dpi monochrome A4/Letter raster adapter and independent pixel round-trip tests.
- Add first-boot Wi-Fi credentials through COM, stored in NVS.
- Add host CAPT simulator, IPP integration tests, GitHub build workflow and checksum packaging.
- Mark physical printer/board validation as pending.

### USB web installer r2
- Retry serial initialization with fresh connections, a compatible UART reset and manual BOOT fallback.
- Show the actual connection/download/write error instead of only a generic initialization failure.
- Release the COM port before the dialog reconnects; preserve the original error if reset fails.
- Verify firmware SHA-256 before erasing; retain UART provisioning and the NVS gap.
- Add installer recovery regression tests to CI. Physical-board verification remains pending.
