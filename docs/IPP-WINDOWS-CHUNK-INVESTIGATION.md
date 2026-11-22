# Repeated IPP upload investigation after SPEED fix1

The physical diagnostic printserver-log (19).txt runs 0.9.8-speed-fix1.
Six of eleven uploads end with an incomplete PWG raster: jobs 1, 3, 6, 7, 8,
and 11. Five stop after exactly 524288 document bytes; jobs 3 and 11 stop
after 1048576 bytes (four stop at 524288: jobs 1, 6, 7 and 8). Successful
Word uploads receive 1454489 bytes. All failed sessions release CAPT ownership.
The diagnostic says body_eof=1, body_failed=0, chunked=1; this is the HTTP
reader's interpretation, not independent proof about the origin of the bytes.

Native repeated test: python3 tools/test_capt_windows_chunks.py
- Five independently restarted server processes with ASan/UBSan.
- Each round sends four 4-page dense PWG documents through Create-Job followed
  by Send-Document, Expect: 100-continue and 512 KiB / 1 MiB HTTP chunks.
- Both immediate and delayed sends, fragmented size lines, chunk extensions,
  trailer, and printer status polling during upload.
- Each round also sends a deliberately truncated document and then a complete
  four-page document to check recovery.
- All 20 complete chunked jobs / 80 pages passed, plus 5 truncation/recovery
  pairs. No spontaneous boundary truncation reproduced.

Document size: 5464088 bytes.
SHA-256: d16f08cdcac0dc8482b4a3b527c0b07808a081ec0ac43bcbeed49aef286e3631

Firmware-specific concern: listen_thread closes newly accepted sockets when
internal free RAM is below 36 KiB, even with only one print worker active.
The physical report contains these refusals. Native host builds omit that
heap check, use eight instead of three workers and retain HTTP connections
where the ESP closes them. Therefore passing native tests does not exclude
an ESP RAM / lwIP / status-poll interoperability bug. No conclusion about a
Windows defect follows from these tests. The refusal's causal connection to
the failed uploads is not established by the current diagnostic.

No firmware change or new device binary is claimed by this investigation.

## ESP HTTP admission model

Added a separate `host/airprint_esp_policy_host` sanitizer build. A native-only
compile flag selects the same three-worker limit, four-second idle timeout and
close-after-request behavior as ESP. The same named heap thresholds are used
for real ESP heap measurements and injected native measurements.

`python3 tools/test_capt_esp_policy.py` passed five independent server runs:
- 15 complete chunked jobs, 60 pages; each input document is 5464088 bytes.
- 512 KiB and 1 MiB chunks with a slowed simulated printer.
- After starting the real print pipeline, set heap admission to physical log
  snapshots: free=33323/largest=28672 and free=35211/largest=31744.
- Also inject fragmentation: free=50000/largest=14335.
- Verify control sockets really close without a response while the existing
  print socket delivers the remaining document, final chunk and success reply.
- Verify a fourth concurrent socket is refused; verify HTTP Connection: close
  is sent even when the request asks for keep-alive.
- Deliberate document truncation still returns IPP 0x0411 in each run.

This reproduces the connection refusal but does not reproduce spontaneous
truncation of the active print stream. The low-heap threshold alone is not
sufficient to cause that failure in this model. It is not a validation of the
ESP allocator, task stack consumption, Xtensa CPU, DMA, Wi-Fi radio, USB host or
lwIP; those remain untested. No physical device was connected or flashed.
The test-only build retains native sanitizer stack sizes, and injected heap
numbers affect admission only: they do not impose a real process memory limit.
No runtime dependency on environment variables or heap files is added to ESP.

Repeated after sharing the production heap threshold constants: another five
server runs / 15 complete jobs / 60 pages passed. Combined policy runs: ten
independent processes, 30 complete jobs / 120 pages, plus ten deliberate
truncation checks. No firmware binary was changed or delivered in this step.
