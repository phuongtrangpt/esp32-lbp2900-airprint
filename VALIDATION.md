## 0.9.10

Lưu tùy chọn âm báo trên trình duyệt, khôi phục qua thao tác người dùng khi cần. Đèn RGB cảnh báo 5 nháy đỏ / nghỉ 2 giây từ trạng thái firmware, ưu tiên hơn nhận diện và in. Kiểm thử 20 chu kỳ LED, lưu/tải lại âm báo, lỗi quyền phát và kết nối lại. Cần kiểm tra đèn trên board thật.

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

## 0.6.0 HP P1006 native 2-bit mode

Root-cause candidate from physical report: printer consumes and ejects a blank
page with earlier 1-bit mode. Upstream P1006 PPD specifies 1200x600 (2-bit video).
New native A4/Letter two-page streams match upstream foo2xqx -r1200x600 exactly,
including BIH/raster dimensions, VIDEO_BPP=2 and compressed pixels. Client still
supplies 600-dpi gray/RGB. Source metadata is not dumped; hardware log now counts
ink pixels to distinguish a blank source from a printer decoding problem.

Native allocation accounting: HP A4 100 pages retained 4,370,064, peak 6,435,272;
two dense noise pages peak 6,500,424; Letter 100 pages peak 6,215,968 bytes.
Zero growth after pages and zero tracked allocations after each job; four induced
compression OOM cases recover. This is not an ESP heap measurement. Physical
retest still required. Native CAPT regression passes; firmware compilation runs
in CI because this recovered workspace has no local ESP toolchain cache.

## v0.5.0-diagnostics-preview

ESP build and native CAPT/IPP regressions verify the logging adapter does not
change printer bytes. Node VM tests run the actual C-embedded page script for
initial/automatic/manual refresh, pause, error/recovery and plain-text log data.
Playwright desktop/mobile tests cover page layout, download and error recovery
in CI. Local Chromium execution is blocked by the environment's Unix socket
restriction; no local browser pass is claimed. ESP HTTP handler/ring/RTC/core dump
behaviour still requires hardware; QEMU is unavailable here.

Logs: bounded 64 KiB PSRAM ring (8 KiB fallback), existing 3 KiB RTC tail; no
continuous flash writes. Snapshot export uses a 1536-byte working buffer; slow
readers may lose overwritten old bytes and receive an explicit notice. No print
payload or Wi-Fi password is dumped. Core dumps may contain process memory.

## v0.4.0-standard-preview: measured results

Native ASan/UBSan + wrapped malloc/calloc/realloc/free accounting (64-bit host;
not ESP heap measurements). All figures include driver allocations, not network,
FreeRTOS, USB stacks, TLS or browser memory.

| Test | Retained job bytes | Peak allocated bytes | Growth after each page | After job |
|---|---:|---:|---:|---:|
| Canon LBP2900, 100 A4 pages | 190440 | 190440 | 0 | 0 |
| HP P1006, 100 A4 pages | 4214048 | 5266936 | 0 | 0 |
| HP P1006, 2 dense random A4 pages | 4214048 | 5328016 | 0 | 0 |
| HP P1006, 100 Letter pages | 4064256 | 5079344 | 0 | 0 |
| PCL5, 100 A4 pages | 1870 | 1870 | 0 | 0 |
| PostScript, 100 A4 pages | 1870 | 1870 | 0 | 0 |

Four injected HP compression allocation failures recover and leave zero tracked
allocations after ending the failed job. This checks OOM cleanup, not USB recovery.
Generic stream tests validate all PCL raster pixels and render two PS pages using
Ghostscript, for both A4/Letter and 300/600 dpi with gray/RGB source. Selection tests
reject PCLXL-only, PCL6-only, PCL3GUI, PCLm, SPL, UFRII, ZJS and misleading prefixes.

Firmware: 1,720,419 application bytes (previous 1,717,151: +3,268 bytes); static RAM
77,092 bytes, unchanged. App slot 4 MiB, board flash 16 MiB and PSRAM 8 MiB.
100-page IPP integration is included through TEST_CAPT_PAGES=100.

No printer/ESP hardware is attached. Native results do not prove actual PSRAM
availability/fragmentation, USB completion, printer imageable area or paper output.
QEMU unavailable in this environment; no QEMU result is claimed. Browser rendering
still accumulates page Blobs; the bounded ESP path does not bound browser RAM.

## HP P1006 v0.3.0

Native tests cover USB and IEEE1284 matching, A4/Letter, gray/RGB two-page streams,
line order rejection and sink errors. Optional `tools/test_hp_reference.py PATH_TO_FOO2XQX`
compares emitted XQX bytes against the independent upstream encoder.
No physical ESP or printer is connected. HPDL upload/persistence, printer boot timing,
USB re-enumeration and paper output still require hardware verification. HPDL is not bundled.

# Validation record — 0.2.0-web-preview

This is an experimental CAPT port, not a hardware-qualified printer release.

## Completed

- Portable C driver builds with GCC, AddressSanitizer and UndefinedBehaviorSanitizer.
- `host/capt_host`: two-page A4 grayscale and Letter RGB jobs; all bands independently
  decompressed and compared bit by bit with the expected cropped raster. Includes
  full-buffer streaming, final short bands, white lines, end-of-band/end-of-page markers and padding.
- Fragmented binary replies (three bytes per read), failed read at job start and
  during data transmission, wrong command reply, unfinished page and invalid line order.
- `tools/test_capt_ipp.py`: actual HTTP/IPP server receives a two-page 600 dpi URF
  document, produces CAPT, reports completed/two pages, rejects 300 dpi job settings,
  advertises LBP2900 and reports no scanner; a malformed raster aborts its job,
  and a subsequent valid job completes.
- Upstream BJL driver selftest: zero failures. Scanner protocol and OLED selftests pass.
- Web TypeScript/Svelte check: zero errors and warnings; 13 existing unit tests pass.
- Web production assets generated successfully with Vite.
- Upstream Gutenprint comparison ran successfully for the original PIXMA variants
  (reference streams identical, apart from the documented extra blank padding lines).
  This checks preserved PIXMA code; it is not evidence of CAPT hardware compatibility.

LeakSanitizer cannot operate under this environment's process tracing. Tests use
`ASAN_OPTIONS=detect_leaks=0`; address/undefined-behavior checks remain active.

## Not established

- Physical LBP2900 USB enumeration, USB transfer timing, CAPT acknowledgments,
  power-up/warm-up behavior, real paper output, speed, toner quality, and recovery.
- iOS/macOS/Android discovery and printing on a physical LAN.
- IPP conformance suite: the upstream script was attempted, but `ipptool`/`cupsfilter`
  are absent here. Its `0 pass, 0 fail` summary does not count as a pass.
- Full browser end-to-end suite from upstream: it assumes the PIXMA colour/scanner
  flow and 300 dpi printing and was not completed for this CAPT fork.
- QEMU device/USB simulation: QEMU is unavailable here. QEMU cannot establish real
  CAPT USB behavior in any case. CAPT protocol tests use the host simulator.

A successful simulator run validates internal encoding and error behavior only.
The simulator follows protocol assumptions and cannot confirm those assumptions on
real Canon hardware. In particular, binary reply lengths, endpoint behavior and
streaming under a full printer buffer require physical validation.

Firmware compilation and package checks are recorded in BUILD_STATUS.md.

## Browser installer / Improv Serial

- Firmware compiles for ESP32-S3 N16R8 with the UART driver and Improv service.
- Host parser tests exercise fragmented frames, checksums, resynchronization,
  malformed RPC lengths, Wi-Fi credential bounds, open networks, embedded NULs
  and every outer packet length from 0 to 255 under ASan/UBSan.
- Web assets are produced from a checksum-verified package. Byte comparisons
  verify the OTA/app parts, and assertions prevent any part overlapping NVS.
- The installer is bundled locally, with third-party license notices.
- Physical Web Serial flashing, actual Wi-Fi provisioning and LBP2900 output
  cannot be confirmed without the user's board and printer.

## USB installer r2 (2026-11-16)

User reports the same board/cable flashes successfully on another site, while this
installer reports `Failed to initialize`. This is not yet reproduced on a physical
board here; do not claim a confirmed hardware root cause or a hardware-validated fix.

Changes: build ESP Web Tools from its module entry so the local flash adapter is
used; retry initialization with a fresh loader and released transport, separate
DTR/RTS UART reset, then no-reset/manual BOOT mode. Preserve detailed errors,
release serial before publishing terminal events, and catch erase/download/reset
failures. Keep UART-only selection because firmware Improv runs on UART0.
Check all image SHA-256 values before erasing; retain the NVS gap and OTA reset.

Validation: ten adapter/recovery and package tests pass (`npm --prefix installer test`),
production Vite build passes, Improv Serial host tests pass. Downloaded current
production 0.5.0 package and verified S3 image headers, ota_0 offset 0x20000,
4 MiB app bound, 16 MiB flash bound and NVS gap. Firmware sources unchanged.
Local Chromium smoke test could not launch due to environment socket restrictions;
USB communication, reset timing and Wi-Fi setup still require the user's board.
