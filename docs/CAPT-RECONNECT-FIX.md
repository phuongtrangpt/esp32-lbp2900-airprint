# Clear recovery warnings after USB reattach

Based on the exact source-code.tar.gz supplied in
LBP2900-SPEED-USB-TEST-0146673.zip (SHA-256 of the original ZIP recorded below).
All source paths were compared with review/canon-capt-recovery b02e754; the five
differing files were restored byte-for-byte before applying this fix. The SPEED
page pipeline and CAPT protocol edits are retained. Version: 0.9.8-speed-fix1.
The suffix 0146673 is archive provenance, not a remotely resolvable Git commit.


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

Original supplied ZIP SHA-256: 5326fc4c6526f61f28b55b0e88061c1ba380f7d66e3b2e21ecaf154b7aaec9b5
