# Clear recovery warnings after USB reattach

Based on review/canon-capt-recovery (b02e754). That branch contains the exact
Vietnamese restart warning reported by the user; main does not contain it.
The supplied diagnostic report identifies itself as 0.9.8-speed-test; its exact
build commit is not recorded in the report.

On every USB attach, invalidate the previous session's status callbacks, clear
paper/restart warnings, and invalidate the cached status. A failed cleanup from
the previous USB session cannot relatch a warning after reattachment. New faults
from the current session still appear normally. This clears software warnings;
it does not claim that a physical jam has been resolved.

Validation in the development container:
- make -C host all: passed (existing vendor warnings).
- ASAN_OPTIONS=detect_leaks=0 host/reconnect_host: passed, including immediate
  cache refresh, stale cleanup callbacks and new-session fault reporting.
- ASAN_OPTIONS=detect_leaks=0 host/capt_host: passed.
- python3 tools/test_capt_ipp.py: passed.
- python3 tools/test_capt_scheduler.py: passed.
- node tools/test_device_pages.mjs: passed.
- web npm run check: zero errors/warnings; npm test: 14 passed.
- pio run -e esp32s3 and tools/qemu_selftest.sh: unavailable locally because
  PlatformIO (pio) is not installed. GitHub build workflow builds device profiles
  and now runs the reconnect regression as well.

No physical board/printer is attached. No device was flashed or rebooted.
