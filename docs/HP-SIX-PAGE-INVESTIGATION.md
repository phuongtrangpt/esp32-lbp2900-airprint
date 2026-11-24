# HP P1006 six-page IPP investigation

Base: main 3a9b72b, HP 0.9.10-hp-raw12-net2.
The user selected PDF pages 5–10 in Windows; pages 5–9 printed, page 10 did not.
The log reports final HTTP chunk with incomplete raster after exactly 9,961,472
raster bytes (9.5 MiB). No USB timeout is present in that excerpt.

Compared NET1 test commit 847c9f9 with current main: no differences in
components/airprint, HP driver, USB transport, HP RAW bridge or platformio.ini.
This does not identify the user's earlier successful test without its version.

The generic host does not define PRINTSERVER_PROFILE_HP, so previous generic
IPP tests did not exercise HP's dedicated read-ahead path. hp_ipp_host now does.

Test: 6-page 2480x3507 300-dpi gray URF, 10,504,536 raster bytes, Create-Job +
Send-Document, 512-KiB HTTP chunks, fragmented chunk headers and payloads, chunk
extensions and trailers, simulated slow printer writes. Run three complete
jobs each with HP read-ahead and forced direct-stream fallback. Intentionally
terminate at 9.5 MiB in each mode, then send a complete six-page recovery job.

Result: all eight complete jobs finish six pages. Both intentional short jobs
abort at page six after five complete pages with IPP 0x0411, matching the log.
No ASan/UBSan errors. No spontaneous 9.5-MiB truncation reproduced.

This is not physical ESP allocator/lwIP/radio or Windows spooler emulation.
It does not prove the cause is Windows, or that a device-side timing/memory
issue is absent. The full device log including firmware identity, job admission,
read-ahead availability/capacity and preceding network events is still needed.

No firmware fix or official deployment is made from these findings. The only
production-source edit is a native-only allocation-failure hook, excluded when
ESP_PLATFORM is defined; all ESP code remains identical.

Run:
  make -C host hp_ipp_host
  ASAN_OPTIONS=detect_leaks=0 python tools/test_hp_six_page_upload.py
