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
