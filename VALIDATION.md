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
