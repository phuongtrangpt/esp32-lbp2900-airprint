# CAPT source provenance

Vendored from https://github.com/ValdikSS/captdriver, branch val, commit 892f320c019974a5b737e000fcc2c8582264a131.
License: GPL-3.0-or-later, see COPYING and original file headers.

Modifications: replace CUPS transport with portable read/write/flush callbacks;
replace process exits with guarded job failure; export only LBP2900 ops;
remove large status stack buffer; use portable UTC job timestamps; validate payload lengths.
Upstream protocol state machine and Hi-SCoA compressor are retained.
The complete combined firmware is distributed under GPL-3.0-or-later,
with original MIT notices for esp32-usb-to-airprint preserved.
