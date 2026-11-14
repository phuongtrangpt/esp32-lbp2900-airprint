# Changelog

## 0.1.0-capt-preview

- Fork ESP32-S3 AirPrint/IPP and web UI from ngxson/esp32-usb-to-airprint.
- Port LBP2900 CAPT state machine and Hi-SCoA compression from ValdikSS/captdriver.
- Add binary USB replies, bounded CAPT read/write waits and per-job error unwinding.
- Add 600 dpi monochrome A4/Letter raster adapter and independent pixel round-trip tests.
- Add first-boot Wi-Fi credentials through COM, stored in NVS.
- Add host CAPT simulator, IPP integration tests, GitHub build workflow and checksum packaging.
- Mark physical printer/board validation as pending.
