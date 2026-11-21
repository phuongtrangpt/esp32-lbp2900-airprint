#!/usr/bin/env python3
"""Regression guard for Canon quality settings HTTP routing and IPP exposure.

Native tests do not run ESP-specific NVS/UI handlers; this source-level check
ensures the normal firmware does not silently omit their dispatcher registration.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/airprint_esp.c").read_text()
ipp = (root / "components/airprint/airprint.c").read_text()
web = (root / "web/src/views/PrinterView.svelte").read_text()

start = src.index("static bool http_handler(")
end = src.index("// Most of the PSRAM", start)
normal = src[start:end]
assert "print_quality_http(h,method,path)" in normal, "Normal HTTP handler must route quality GET and POST"
assert normal.index("print_quality_http(h,method,path)") < normal.index("web_http(h, method, path)"), "Settings API must precede web fallback"

assert '"/api/print-quality"' in src
assert '"print_dpi"' in src and 'nvs_commit(nh)' in src
assert "s_preferred_dpi = dpi" in src, "Saved preference must become effective without restart"
assert "preferred_dpi" in ipp
assert "printer-resolution-default" in ipp and "printer-resolution-supported" in ipp
assert "print-quality" in web and "Lưu độ phân giải" in web
print("PASS: Quality HTTP GET/POST route, persistent NVS, IPP attributes, and web settings UI")

# Regression: a long-running print job must never inherit the short idle
# browser timeout; CAPT raster conversion can take multiple seconds/page.
assert "#define ACTIVE_IPP_TIMEOUT_S 120" in ipp
assert "setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &active_tv" in ipp
assert 'raster stream truncated:' in ipp
print("PASS: active IPP upload has long receive timeout and truncation diagnostics")
