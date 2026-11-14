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
