#!/usr/bin/env python3
"""Check that the browser submits high-quality jobs via its dedicated route."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ipp = (root / "components/airprint/airprint.c").read_text()
browser_ipp = (root / "web/src/lib/ipp.ts").read_text()
browser_print = (root / "web/src/lib/print.ts").read_text()

assert '"/ipp/web-print"' in ipp and '"/ipp/web-print"' in browser_ipp
assert "xhr.open('POST', WEB_PRINT_PATH)" in browser_print
print("PASS: Web print route matches the firmware")

# Regression: a long-running print job must never inherit the short idle
# browser timeout; CAPT raster conversion can take multiple seconds/page.
assert "#define ACTIVE_IPP_TIMEOUT_S 120" in ipp
assert "ACTIVE_IPP_TIMEOUT_S - ACTIVE_IPP_SLICE_S" in ipp
assert "set_recv_timeout(c, ACTIVE_IPP_SLICE_S)" in ipp
assert 'raster stream truncated:' in ipp
print("PASS: active IPP upload has long receive timeout and truncation diagnostics")
