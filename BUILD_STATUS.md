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
