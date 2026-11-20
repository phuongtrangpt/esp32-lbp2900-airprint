# v0.9.4 — USB latency and Canon draft input

The real-device v0.9.3 report completed 14 A4 600 dpi pages in about 348 s including finalization. The pipeline reported 30.0 s input waiting, 218.8 s line processing (including synchronous CAPT response/flush waits), and 92.6 s residual decoding/page setup. These counters are not a pure CPU profile.

Changes:
- Wake the USB client immediately when a binary input transfer is requested, rather than waiting for its 50 ms event-loop timeout.
- Block on completion semaphores for IN replies and OUT flushes, rather than adding a 10 ms polling delay to each transfer. Completion callbacks remain owned by the USB client. Cancellation checks and existing transport timeouts remain.
- Yield from the CAPT transport at a bounded 20 ms interval rather than at every command/header/body check. The raster pipeline and blocked USB operations still allow other tasks to run.
- Route repetitive per-band video debug logs to debug level; retain job summaries, errors and recovery messages.
- Add separate USB response/flush wait totals and CAPT compression/band-transfer totals to diagnostics.

Canon LBP2900 accepts 300 and 600 dpi input; 600 stays the default. Draft 300 input has one quarter as many pixels and is expanded by nearest-neighbor replication into the unchanged 600 dpi CAPT engine geometry, margins and HiSCoA format. This reduces input/rendering work; it is not a native 300 dpi engine command and does not promise a fourfold speedup. Fine text and graphics can lose detail. HP P1006 and other driver resolutions are unchanged; the web UI shows draft only when advertised by the connected firmware.

Validation: independent HiSCoA decoding checks 300/600 dpi checkerboards in grayscale/RGB on A4/Letter/A5/A6, two pages per case; IPP tests include 300 dpi input, unsupported 150 dpi rejection and a 100-page 600 dpi stream. No physical speed measurement is available in the development environment. Reprint the same document on the same board/printer and compare elapsed times and the new diagnostic counters before claiming an improvement in pages per minute. Canon A6 physical feed remains separately unverified.
