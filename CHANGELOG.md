## 0.9.9 chính thức

Thay thế bản 0.9.8: giữ nền SPEED từ gói 0146673 và sửa trạng thái lỗi sau kết nối lại USB. Kiểm thử mô phỏng không thay thế kiểm chứng máy thật. Lỗi IPP ngắt sớm ở 512 KB/1 MB vẫn đang điều tra; xem docs/IPP-WINDOWS-CHUNK-INVESTIGATION.md.

## 0.8.0-vietphuong · vietphuong esp

Brand and support: https://www.facebook.com/leviethoaiphuong.
Setup AP `vietphuong esp-XXXX`, WPA2 default password `vietphuongesp`; first boot
without saved Wi-Fi opens setup. Captive DNS responds only on AP address
192.168.4.1; fallback browser URL http://192.168.4.1/setup. Phones do not need USB.
Hold BOOT 5 seconds after boot for maintenance/setup; hold 10 seconds for DHCP
recovery and setup after restart. Saved devices require a physical maintenance
window before changing Wi-Fi through the portal. Credentials save after GOT_IP;
invalid attempts keep previous saved settings and allow retry. AP closes 30 seconds
after successful setup. The shared default AP password is not admin authentication.
Handover at /setup clears saved Wi-Fi/IP only, preserves HP firmware, then reboots
when idle. Always use credential-free public builds for customer devices.
/update supports user-triggered HTTPS version checks and OTA with progress.
Install requires BOOT confirmation within 120 seconds; no updates while printing
or provisioning. New jobs are refused while update/provisioning is reserved.
SHA-256, exact byte count, chip image validation and app project_name/profile/version
checks precede boot-slot selection. Only the inactive app partition is written;
NVS/HP data remain. Existing bootloader rollback checks Wi-Fi health after reboot.
TLS trust and SHA-256 are used; no new device secure-boot/signing-key system is claimed.
Cloud manifests/images are static and default to the existing free GitHub Pages
origin. Configure FIRMWARE_UPDATE_BASE_URL when moving firmware hosting to Cloudflare.
Source remains on GitHub and is linked under Licenses, with matching source in ZIP.
Native/CI checks are used; captive popup, Wi-Fi/AP radio and OTA power-cut/recovery
remain pending physical validation. No board attached; QEMU unavailable locally.

## 0.7.0-model-profiles

Dedicated HP P1006 and Canon LBP2900 builds with model-specific ESP 32 names.
Static IPv4/DHCP settings at /network persist in NVS; BOOT held 10 seconds after
startup restores DHCP without deleting Wi-Fi/HP data. Invalid IP/mask/gateway/DNS
are rejected before saving; saving while a USB print job is active is refused.
HP bootstrap automatically downloads, verifies a pinned SHA256 and caches the
upstream firmware; public packages do not redistribute the proprietary blob.
Windows offline launcher prepares the HP cache once online, then serves both
profiles from localhost without an installed runtime. ZIP includes matching source.
Native network validation, tar/HPDL conversion, profile isolation, and Go cache
preparation tests added. Firmware builds and deployment are checked in CI.
The user confirmed HP A4/Letter test printing with v0.6; new v0.7 network/USB flow
and Canon paper output still need physical verification. QEMU unavailable locally.

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
